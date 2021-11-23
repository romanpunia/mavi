#include "http.h"
#include "../script/std-lib.h"
#ifdef TH_MICROSOFT
#include <WS2tcpip.h>
#include <io.h>
#include <wepoll.h>
#else
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#endif
#include <future>
#include <random>
#include <string>
extern "C"
{
#ifdef TH_HAS_ZLIB
#include <zlib.h>
#endif
#ifdef TH_HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/engine.h>
#include <openssl/conf.h>
#include <openssl/dh.h>
#endif
}
#define WEBSOCKET_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

namespace Tomahawk
{
	namespace Network
	{
		namespace HTTP
		{
			static void TextAppend(std::vector<char>& Array, const std::string& Src)
			{
				Array.insert(Array.end(), Src.begin(), Src.end());
			}
			static void TextAppend(std::vector<char>& Array, const char* Buffer, size_t Size)
			{
				Array.insert(Array.end(), Buffer, Buffer + Size);
			}
			static void TextAssign(std::vector<char>& Array, const std::string& Src)
			{
				Array.assign(Src.begin(), Src.end());
			}
			static void TextAssign(std::vector<char>& Array, const char* Buffer, size_t Size)
			{
				Array.assign(Buffer, Buffer + Size);
			}
			static std::string TextSubstring(std::vector<char>& Array, size_t Offset, size_t Size)
			{
				return std::string(Array.data() + Offset, Size);
			}
			static std::string TextHTML(const std::string& Result)
			{
				if (Result.empty())
					return Result;

				return Core::Parser(Result).Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\n", "<br>").R();
			}

			MimeStatic::MimeStatic(const char* Ext, const char* T) : Extension(Ext), Type(T)
			{
			}

			WebSocketFrame::WebSocketFrame(Socket* NewStream) : State((uint32_t)WebSocketState::Open), Active(true), Reset(false), Stream(NewStream), Codec(new WebCodec())
			{
			}
			WebSocketFrame::~WebSocketFrame()
			{
				while (!Messages.empty())
				{
					auto& Next = Messages.front();
					TH_FREE(Next.Buffer);
					Messages.pop();
				}

				TH_RELEASE(Codec);
			}
			void WebSocketFrame::Send(const char* Buffer, size_t Size, WebSocketOp Opcode, const WebSocketCallback& Callback)
			{
				Send(0, Buffer, Size, Opcode, Callback);
			}
			void WebSocketFrame::Send(unsigned int Mask, const char* Buffer, size_t Size, WebSocketOp Opcode, const WebSocketCallback& Callback)
			{
				TH_ASSERT_V(Buffer != nullptr, "buffer should be set");

				Section.lock();
				if (EnqueueNext(Mask, Buffer, Size, Opcode, Callback))
					return Section.unlock();
				Section.unlock();

				unsigned char Header[14];
				size_t HeaderLength = 1;
				Header[0] = 0x80 + ((size_t)Opcode & 0xF);

				if (Size < 126)
				{
					Header[1] = (unsigned char)Size;
					HeaderLength = 2;
				}
				else if (Size <= 65535)
				{
					uint16_t Length = htons((uint16_t)Size);
					Header[1] = 126;
					HeaderLength = 4;
					memcpy(Header + 2, &Length, 2);
				}
				else
				{
					uint32_t Length1 = htonl((uint64_t)Size >> 32);
					uint32_t Length2 = htonl(Size & 0xFFFFFFFF);
					Header[1] = 127;
					HeaderLength = 10;
					memcpy(Header + 2, &Length1, 4);
					memcpy(Header + 6, &Length2, 4);
				}

				if (Mask)
				{
					Header[1] |= 0x80;
					memcpy(Header + HeaderLength, &Mask, 4);
					HeaderLength += 4;
				}

				Stream->WriteAsync((const char*)Header, HeaderLength, [this, Buffer, Size, Callback](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						if (Size > 0)
						{
							Stream->WriteAsync(Buffer, Size, [this, Callback](NetEvent Event, size_t Sent)
							{
								if (Packet::IsDone(Event))
								{
									if (Callback)
										Callback(this);
									SendNext();
								}
								else if (Packet::IsError(Event))
								{
									Reset = true;
									if (Callback)
										Callback(this);

									if (E.Reset)
										E.Reset(this);
								}
								else if (Packet::IsSkip(Event))
								{
									if (Callback)
										Callback(this);
									SendNext();
								}
							});
						}
						else
						{
							if (Callback)
								Callback(this);
							SendNext();
						}
					}
					else if (Packet::IsError(Event))
					{
						Reset = true;
						if (Callback)
							Callback(this);

						if (E.Reset)
							E.Reset(this);
					}
					else if (Packet::IsSkip(Event))
					{
						if (Callback)
							Callback(this);
						SendNext();
					}
				});
			}
			void WebSocketFrame::SendNext()
			{
				Section.lock();
				if (Stream->HasOutcomingData() || Messages.empty())
					return Section.unlock();

				Message Next = std::move(Messages.front());
				Messages.pop();
				Section.unlock();

				Send(Next.Mask, Next.Buffer, Next.Size, Next.Opcode, Next.Callback);
				TH_FREE(Next.Buffer);
			}
			void WebSocketFrame::Finish()
			{
				if (Reset || State == (uint32_t)WebSocketState::Close)
					return Next();

				State = (uint32_t)WebSocketState::Close;
				Send("", 0, WebSocketOp::Close, [this](WebSocketFrame*)
				{
					Next();
				});
			}
			void WebSocketFrame::Next()
			{
				Section.lock();
			Retry:
				if (State == (uint32_t)WebSocketState::Receive)
				{
					if (E.Dead && E.Dead(this))
					{
						State = (uint32_t)WebSocketState::Close;
						goto Retry;
					}

					Stream->SetReadNotify([this](NetEvent Event, const char*, size_t Recv)
					{
						bool IsDone = Packet::IsDone(Event);
						if (!IsDone && !Packet::IsError(Event))
							return true;

						State = (uint32_t)(IsDone ? WebSocketState::Process : WebSocketState::Close);
						return Core::Schedule::Get()->SetTask([this]()
						{
							Next();
						});
					});
				}
				else if (State == (uint32_t)WebSocketState::Process)
				{
					char Buffer[8192];
					while (true)
					{
						int Size = Stream->Read(Buffer, sizeof(Buffer), [this](NetEvent Event, const char* Buffer, size_t Recv)
						{
							if (Packet::IsData(Event))
								Codec->ParseFrame(Buffer, Recv);

							return true;
						});

						if (Size == -1)
						{
							State = (uint32_t)WebSocketState::Close;
							goto Retry;
						}
						else if (Size == -2)
							break;
					}

					WebSocketOp Opcode;
					State = (uint32_t)WebSocketState::Receive;
					if (!Codec->GetFrame(&Opcode, &Codec->Data))
						goto Retry;

					if (Opcode == WebSocketOp::Text || Opcode == WebSocketOp::Binary)
					{
						if (Opcode == WebSocketOp::Binary)
							TH_TRACE("[websocket] sock %i frame binary\n\t%s", (int)Stream->GetFd(), Compute::Common::HexEncode(Codec->Data.data(), Codec->Data.size()).c_str());
						else
							TH_TRACE("[websocket] sock %i frame text\n\t%.*s", (int)Stream->GetFd(), (int)Codec->Data.size(), Codec->Data.data());

						if (Receive)
						{
							Section.unlock();
							if (Receive(this, Opcode, Codec->Data.data(), (int64_t)Codec->Data.size()))
								return;

							return Next();
						}
					}
					else if (Opcode == WebSocketOp::Ping)
					{
						TH_TRACE("[websocket] sock %i frame ping", (int)Stream->GetFd());
						Section.unlock();
						if (Receive && Receive(this, Opcode, "", 0))
							return;

						return Send("", 0, WebSocketOp::Pong, [this](WebSocketFrame*)
						{
							Next();
						});
					}
					else if (Opcode == WebSocketOp::Close)
					{
						TH_TRACE("[websocket] sock %i frame close", (int)Stream->GetFd());
						Section.unlock();
						if (Receive && Receive(this, Opcode, "", 0))
							return;

						return Finish();
					}
					else if (Receive)
					{
						Section.unlock();
						if (Receive(this, Opcode, "", 0))
							return;

						return Next();
					}
					else
						goto Retry;
				}
				else if (State == (uint32_t)WebSocketState::Close)
				{
					if (!Disconnect)
					{
						Active = false;
						Section.unlock();
						if (E.Close)
							E.Close(this);

						return;
					}
					else
					{
						WebSocketCallback Callback = std::move(Disconnect);
						Section.unlock();
						return Callback(this);
					}
				}
				else if (State == (uint32_t)WebSocketState::Open)
				{
					if (Connect || Receive || Disconnect)
					{
						State = (uint32_t)WebSocketState::Receive;
						if (!Connect)
							goto Retry;

						Section.unlock();
						return Connect(this);
					}
					else
					{
						Section.unlock();
						return Finish();
					}
				}
				Section.unlock();
			}
			bool WebSocketFrame::IsFinished()
			{
				return !Active;
			}
			bool WebSocketFrame::EnqueueNext(unsigned int Mask, const char* Buffer, size_t Size, WebSocketOp Opcode, const WebSocketCallback& Callback)
			{
				if (!Stream->HasOutcomingData())
					return false;

				Message Next;
				Next.Mask = Mask;
				Next.Buffer = (Size > 0 ? (char*)TH_MALLOC(sizeof(char) * Size) : nullptr);
				Next.Size = Size;
				Next.Opcode = Opcode;
				Next.Callback = Callback;

				if (Next.Buffer != nullptr)
					memcpy(Next.Buffer, Buffer, sizeof(char) * Size);

				Messages.emplace(std::move(Next));
				return true;
			}

			GatewayFrame::GatewayFrame(Script::VMCompiler* NewCompiler) : Compiler(NewCompiler), Active(true)
			{
				TH_ASSERT_V(Compiler != nullptr, "compiler should be set");
			}
			void GatewayFrame::Execute(Script::VMContext* Ctx, Script::VMPoll State)
			{
				if (State == Script::VMPoll::Routine || State == Script::VMPoll::Continue)
					return;

				if (State == Script::VMPoll::Exception && E.Exception)
					E.Exception(this);

				if (E.Finish)
					E.Finish(this);
				else
					Finish();
			}
			bool GatewayFrame::Start(const std::string& Path, const char* Method, char* Buffer, size_t Size)
			{
				TH_ASSERT(Buffer != nullptr, false, "buffer should be set");
				TH_ASSERT(Method != nullptr, false, "method should be set");

				if (!Active)
				{
					TH_FREE(Buffer);
					return Finish();
				}

				int Result = Compiler->LoadCode(Path, Buffer, Size);
				TH_FREE(Buffer);

				if (Result < 0)
					return Error(500, "Module cannot be loaded.");

				Result = Compiler->Compile(true);
				if (Result < 0)
					return Error(500, "Module cannot be compiled.");

				Script::VMModule Module = Compiler->GetModule();
				Script::VMFunction Entry = Module.GetFunctionByName("Main");
				if (!Entry.IsValid())
				{
					Entry = Module.GetFunctionByName(Method);
					if (!Entry.IsValid())
						return Error(400, "Method is not allowed.");
				}

				Script::VMContext* Context = Compiler->GetContext();
				Context->SetOnResume(std::bind(&GatewayFrame::Execute, this, std::placeholders::_1, std::placeholders::_2));
				Context->Execute(Entry, nullptr, nullptr);

				return true;
			}
			bool GatewayFrame::Error(int StatusCode, const char* Text)
			{
				if (E.Status)
					E.Status(this, StatusCode, Text);

				return Finish();
			}
			bool GatewayFrame::Finish()
			{
				if (Active)
				{
					if (Compiler != nullptr)
					{
						if (!Compiler->Clear())
							return false;
						TH_CLEAR(Compiler);
					}

					Active = false;
				}

				if (!E.Close)
					return false;

				return E.Close(this);
			}
			bool GatewayFrame::IsFinished()
			{
				return !Active;
			}
			bool GatewayFrame::GetException(const char** Exception, const char** Function, int* Line, int* Column)
			{
				TH_ASSERT(Exception != nullptr, false, "exception ptr should be set");
				TH_ASSERT(Function != nullptr, false, "function ptr should be set");
				TH_ASSERT(Line != nullptr, false, "line ptr should be set");
				TH_ASSERT(Column != nullptr, false, "column ptr should be set");

				Script::VMContext* Context = Compiler->GetContext();
				if (!Context)
					return false;

				*Exception = Context->GetExceptionString();
				*Function = Context->GetExceptionFunction().GetName();
				*Line = Context->GetExceptionLineNumber(Column, nullptr);
				return true;
			}
			Script::VMContext* GatewayFrame::GetContext()
			{
				return (Compiler ? Compiler->GetContext() : nullptr);
			}
			Script::VMCompiler* GatewayFrame::GetCompiler()
			{
				return Compiler;
			}

			SiteEntry::SiteEntry() : Base(TH_NEW(RouteEntry))
			{
				Base->URI = Compute::RegexSource("/");
				Base->Site = this;
			}
			SiteEntry::~SiteEntry()
			{
				for (auto& Group : Groups)
				{
					for (auto* Entry : Group.Routes)
						TH_DELETE(RouteEntry, Entry);
				}

				TH_DELETE(RouteEntry, Base);
				Base = nullptr;
			}
			void SiteEntry::Sort()
			{
				for (auto& Group : Groups)
				{
					qsort((void*)Group.Routes.data(), (size_t)Group.Routes.size(), sizeof(HTTP::RouteEntry*), [](const void* A1, const void* B1) -> int
					{
						HTTP::RouteEntry* A = *(HTTP::RouteEntry**)B1;
						if (A->URI.GetRegex().empty())
							return -1;

						HTTP::RouteEntry* B = *(HTTP::RouteEntry**)A1;
						if (B->URI.GetRegex().empty())
							return 1;

						if (A->Level > B->Level)
							return 1;
						else if (A->Level < B->Level)
							return -1;

						bool fA = A->URI.IsSimple(), fB = B->URI.IsSimple();
						if (fA && !fB)
							return -1;
						else if (!fA && fB)
							return 1;

						return (int)A->URI.GetRegex().size() - (int)B->URI.GetRegex().size();
					});
				}

				qsort((void*)Groups.data(), (size_t)Groups.size(), sizeof(HTTP::RouteGroup), [](const void* A1, const void* B1) -> int
				{
					HTTP::RouteGroup& A = *(HTTP::RouteGroup*)B1;
					if (A.Match.empty())
						return -1;

					HTTP::RouteGroup& B = *(HTTP::RouteGroup*)A1;
					if (B.Match.empty())
						return 1;

					return (int)A.Match.size() - (int)B.Match.size();
				});
			}
			RouteGroup* SiteEntry::Group(const std::string& Match, RouteMode Mode)
			{
				for (auto& Group : Groups)
				{
					if (Group.Match == Match && Group.Mode == Mode)
						return &Group;
				}

				HTTP::RouteGroup Group;
				Group.Match = Match;
				Group.Mode = Mode;
				Groups.emplace_back(std::move(Group));
				return &Groups.back();
			}
			RouteEntry* SiteEntry::Route(const std::string& Match, RouteMode Mode, const std::string& Pattern)
			{
				if (Pattern.empty() || Pattern == "/")
					return Base;

				HTTP::RouteGroup* Source = nullptr;
				for (auto& Group : Groups)
				{
					if (Group.Match != Match || Group.Mode != Mode)
						continue;

					Source = &Group;
					for (auto* Entry : Group.Routes)
					{
						if (Entry->URI.GetRegex() == Pattern)
							return Entry;
					}
				}

				if (!Source)
				{
					HTTP::RouteGroup Group;
					Group.Match = Match;
					Group.Mode = Mode;
					Groups.emplace_back(std::move(Group));
					Source = &Groups.back();
				}

				HTTP::RouteEntry* From = Base;
				Compute::RegexResult Result;
				Core::Parser Src(Pattern);
				Src.ToLower();

				for (auto& Group : Groups)
				{
					for (auto* Entry : Group.Routes)
					{
						Core::Parser Dest(Entry->URI.GetRegex());
						Dest.ToLower();

						if (Dest.StartsWith("...") && Dest.EndsWith("..."))
							continue;

						if (Src.Find(Dest.R()).Found || Compute::Regex::Match(&Entry->URI, Result, Src.R()))
						{
							From = Entry;
							break;
						}
					}
				}

				return Route(Pattern, Source, From);
			}
			RouteEntry* SiteEntry::Route(const std::string& Pattern, RouteGroup* Group, RouteEntry* From)
			{
				TH_ASSERT(Group != nullptr, nullptr, "group should be set");
				TH_ASSERT(From != nullptr, nullptr, "from should be set");

				HTTP::RouteEntry* Result = TH_NEW(HTTP::RouteEntry, *From);
				Result->URI = Compute::RegexSource(Pattern);
				Group->Routes.push_back(Result);

				return Result;
			}
			bool SiteEntry::Remove(RouteEntry* Source)
			{
				TH_ASSERT(Source != nullptr, false, "source should be set");
				for (auto& Group : Groups)
				{
					auto It = std::find(Group.Routes.begin(), Group.Routes.end(), Source);
					if (It == Group.Routes.end())
						continue;

					TH_DELETE(RouteEntry, *It);
					Group.Routes.erase(It);
					return true;
				}

				return false;
			}
			bool SiteEntry::Get(const char* Pattern, const SuccessCallback& Callback)
			{
				return Get("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Get(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Get = Callback;
				return true;
			}
			bool SiteEntry::Post(const char* Pattern, const SuccessCallback& Callback)
			{
				return Post("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Post(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Post = Callback;
				return true;
			}
			bool SiteEntry::Put(const char* Pattern, const SuccessCallback& Callback)
			{
				return Put("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Put(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Put = Callback;
				return true;
			}
			bool SiteEntry::Patch(const char* Pattern, const SuccessCallback& Callback)
			{
				return Patch("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Patch(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Patch = Callback;
				return true;
			}
			bool SiteEntry::Delete(const char* Pattern, const SuccessCallback& Callback)
			{
				return Delete("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Delete(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Delete = Callback;
				return true;
			}
			bool SiteEntry::Options(const char* Pattern, const SuccessCallback& Callback)
			{
				return Options("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Options(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Options = Callback;
				return true;
			}
			bool SiteEntry::Access(const char* Pattern, const SuccessCallback& Callback)
			{
				return Access("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::Access(const std::string& Match, RouteMode Mode, const char* Pattern, const SuccessCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.Access = Callback;
				return true;
			}
			bool SiteEntry::WebSocketConnect(const char* Pattern, const WebSocketCallback& Callback)
			{
				return WebSocketConnect("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::WebSocketConnect(const std::string& Match, RouteMode Mode, const char* Pattern, const WebSocketCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.WebSocket.Connect = Callback;
				return true;
			}
			bool SiteEntry::WebSocketDisconnect(const char* Pattern, const WebSocketCallback& Callback)
			{
				return WebSocketDisconnect("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::WebSocketDisconnect(const std::string& Match, RouteMode Mode, const char* Pattern, const WebSocketCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.WebSocket.Disconnect = Callback;
				return true;
			}
			bool SiteEntry::WebSocketReceive(const char* Pattern, const WebSocketReadCallback& Callback)
			{
				return WebSocketReceive("", RouteMode::Start, Pattern, Callback);
			}
			bool SiteEntry::WebSocketReceive(const std::string& Match, RouteMode Mode, const char* Pattern, const WebSocketReadCallback& Callback)
			{
				HTTP::RouteEntry* Value = Route(Match, Mode, Pattern);
				if (!Value)
					return false;

				Value->Callbacks.WebSocket.Receive = Callback;
				return true;
			}

			MapRouter::MapRouter() : VM(nullptr)
			{
			}
			MapRouter::~MapRouter()
			{
				for (auto& Entry : Sites)
					TH_DELETE(SiteEntry, Entry.second);
			}
			SiteEntry* MapRouter::Site(const char* Pattern)
			{
				TH_ASSERT(Pattern != nullptr, nullptr, "pattern should be set");
				auto It = Sites.find(Pattern);
				if (It != Sites.end())
					return It->second;

				std::string Name = Core::Parser(Pattern).ToLower().R();
				if (Name.empty())
					Name = "*";

				HTTP::SiteEntry* Result = TH_NEW(HTTP::SiteEntry);
				Sites[Name] = Result;

				return Result;
			}

			void Resource::PutHeader(const std::string& Label, const std::string& Value)
			{
				TH_ASSERT_V(!Label.empty(), "label should not be empty");
				Headers[Label].push_back(Value);
			}
			void Resource::SetHeader(const std::string& Label, const std::string& Value)
			{
				TH_ASSERT_V(!Label.empty(), "label should not be empty");
				auto& Range = Headers[Label];
				Range.clear();
				Range.push_back(Value);
			}
			RangePayload* Resource::GetHeaderRanges(const std::string& Label)
			{
				TH_ASSERT(!Label.empty(), nullptr, "label should not be empty");
				return (RangePayload*)&Headers[Label];
			}
			const std::string* Resource::GetHeaderBlob(const std::string& Label) const
			{
				TH_ASSERT(!Label.empty(), nullptr, "label should not be empty");
				auto It = Headers.find(Label);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				const std::string& Result = It->second.back();
				return &Result;
			}
			const char* Resource::GetHeader(const std::string& Label) const
			{
				TH_ASSERT(!Label.empty(), nullptr, "label should not be empty");
				auto It = Headers.find(Label);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				return It->second.back().c_str();
			}

			void RequestFrame::SetMethod(const char* Value)
			{
				TH_ASSERT_V(Value != nullptr, "value should be set");
				strncpy(Method, Value, sizeof(Method));
			}
			void RequestFrame::SetVersion(unsigned int Major, unsigned int Minor)
			{
				std::string Value = "HTTP/" + std::to_string(Major) + '.' + std::to_string(Minor);
				strncpy(Version, Value.c_str(), sizeof(Version));
			}
			void RequestFrame::PutHeader(const std::string& Key, const std::string& Value)
			{
				TH_ASSERT_V(!Key.empty(), "key should not be empty");
				Headers[Key].push_back(Value);
			}
			void RequestFrame::SetHeader(const std::string& Key, const std::string& Value)
			{
				TH_ASSERT_V(!Key.empty(), "key should not be empty");
				auto& Range = Headers[Key];
				Range.clear();
				Range.push_back(Value);
			}
			RangePayload* RequestFrame::GetCookieRanges(const std::string& Key)
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				return (RangePayload*)&Cookies[Key];
			}
			const char* RequestFrame::GetCookie(const std::string& Key) const
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				auto It = Cookies.find(Key);
				if (It == Cookies.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				return It->second.back().c_str();
			}
			RangePayload* RequestFrame::GetHeaderRanges(const std::string& Key)
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				return (RangePayload*)&Headers[Key];
			}
			const std::string* RequestFrame::GetHeaderBlob(const std::string& Key) const
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				auto It = Headers.find(Key);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				const std::string& Result = It->second.back();
				return &Result;
			}
			const char* RequestFrame::GetHeader(const std::string& Key) const
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				auto It = Headers.find(Key);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				return It->second.back().c_str();
			}
			std::vector<std::pair<int64_t, int64_t>> RequestFrame::GetRanges() const
			{
				const char* Range = GetHeader("Range");
				if (Range == nullptr)
					return std::vector<std::pair<int64_t, int64_t>>();

				std::vector<std::string> Bases = Core::Parser(Range).Split(',');
				std::vector<std::pair<int64_t, int64_t>> Ranges;

				for (auto& Item : Bases)
				{
					Core::Parser::Settle Result = Core::Parser(&Item).Find('-');
					if (!Result.Found)
						continue;

					const char* Start = Item.c_str() + Result.Start;
					uint64_t StartLength = 0;

					while (Result.Start > 0 && *Start-- != '\0' && isdigit(*Start))
						StartLength++;

					const char* End = Item.c_str() + Result.Start;
					uint64_t EndLength = 0;

					while (*End++ != '\0' && isdigit(*End))
						EndLength++;

					int64_t From = std::stoll(std::string(Start, StartLength));
					if (From == -1)
						break;

					int64_t To = std::stoll(std::string(End, EndLength));
					if (To == -1 || To < From)
						break;

					Ranges.emplace_back(std::make_pair((uint64_t)From, (uint64_t)To));
				}

				return Ranges;
			}
			std::pair<uint64_t, uint64_t> RequestFrame::GetRange(std::vector<std::pair<int64_t, int64_t>>::iterator Range, uint64_t ContenLength) const
			{
				if (Range->first == -1 && Range->second == -1)
					return std::make_pair(0, ContenLength);

				if (Range->first == -1)
				{
					Range->first = ContenLength - Range->second;
					Range->second = ContenLength - 1;
				}

				if (Range->second == -1)
					Range->second = ContenLength - 1;

				return std::make_pair(Range->first, Range->second - Range->first + 1);
			}

			void ResponseFrame::PutBuffer(const std::string& Text)
			{
				TextAppend(Buffer, Text);
			}
			void ResponseFrame::SetBuffer(const std::string& Text)
			{
				TextAssign(Buffer, Text);
			}
			void ResponseFrame::PutHeader(const std::string& Key, const std::string& Value)
			{
				TH_ASSERT_V(!Key.empty(), "key should not be empty");
				Headers[Key].push_back(Value);
			}
			void ResponseFrame::SetHeader(const std::string& Key, const std::string& Value)
			{
				TH_ASSERT_V(!Key.empty(), "key should not be empty");
				auto& Range = Headers[Key];
				Range.clear();
				Range.push_back(Value);
			}
			void ResponseFrame::SetCookie(const char* Key, const char* Value, uint64_t Expires, const char* Domain, const char* Path, bool Secure, bool HTTPOnly)
			{
				TH_ASSERT_V(Key != nullptr, "key should be set");
				TH_ASSERT_V(Value != nullptr, "value should be set");

				for (auto& Cookie : Cookies)
				{
					if (Core::Parser::CaseCompare(Cookie.Name.c_str(), Key) != 0)
						continue;

					if (Domain != nullptr)
						Cookie.Domain = Domain;

					if (Path != nullptr)
						Cookie.Path = Path;

					Cookie.Value = Value;
					Cookie.Secure = Secure;
					Cookie.Expires = Expires;
					Cookie.HTTPOnly = HTTPOnly;
					return;
				}

				Cookie Cookie;
				Cookie.Name = Key;
				Cookie.Value = Value;
				Cookie.Secure = Secure;
				Cookie.Expires = Expires;
				Cookie.HTTPOnly = HTTPOnly;

				if (Domain != nullptr)
					Cookie.Domain = Domain;

				if (Path != nullptr)
					Cookie.Path = Path;

				Cookies.emplace_back(std::move(Cookie));
			}
			Cookie* ResponseFrame::GetCookie(const char* Key)
			{
				TH_ASSERT(Key != nullptr, nullptr, "key should be set");
				for (uint64_t i = 0; i < Cookies.size(); i++)
				{
					Cookie* Result = &Cookies[i];
					if (!Core::Parser::CaseCompare(Result->Name.c_str(), Key))
						return Result;
				}

				return nullptr;
			}
			RangePayload* ResponseFrame::GetHeaderRanges(const std::string& Key)
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				return (RangePayload*)&Headers[Key];
			}
			const std::string* ResponseFrame::GetHeaderBlob(const std::string& Key) const
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				auto It = Headers.find(Key);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				const std::string& Result = It->second.back();
				return &Result;
			}
			const char* ResponseFrame::GetHeader(const std::string& Key) const
			{
				TH_ASSERT(!Key.empty(), nullptr, "key should not be empty");
				auto It = Headers.find(Key);
				if (It == Headers.end())
					return nullptr;

				if (It->second.empty())
					return nullptr;

				return It->second.back().c_str();
			}
			std::string ResponseFrame::GetBuffer() const
			{
				return std::string(Buffer.data(), Buffer.size());
			}
			bool ResponseFrame::HasBody() const
			{
				return Util::ContentOK(Data);
			}
			bool ResponseFrame::IsOK() const
			{
				return StatusCode >= 200 && StatusCode < 400;
			}

			bool Connection::Consume(const ContentCallback& Callback)
			{
				if (Response.Data == Content::Lost || Response.Data == Content::Empty || Response.Data == Content::Saved || Response.Data == Content::Wants_Save)
				{
					if (Callback)
						Callback(this, NetEvent::Finished, nullptr, 0);

					return true;
				}

				if (Response.Data == Content::Corrupted || Response.Data == Content::Payload_Exceeded || Response.Data == Content::Save_Exception)
				{
					if (Callback)
						Callback(this, NetEvent::Timeout, nullptr, 0);

					return true;
				}

				if (Response.Data == Content::Cached)
				{
					if (Callback)
					{
						Callback(this, NetEvent::Packet, Request.Buffer.c_str(), (int)Request.Buffer.size());
						Callback(this, NetEvent::Finished, nullptr, 0);
					}

					return true;
				}

				if ((memcmp(Request.Method, "POST", 4) != 0 && memcmp(Request.Method, "PATCH", 5) != 0 && memcmp(Request.Method, "PUT", 3) != 0) && memcmp(Request.Method, "DELETE", 6) != 0)
				{
					Response.Data = Content::Empty;
					if (Callback)
						Callback(this, NetEvent::Finished, nullptr, 0);

					return false;
				}

				const char* ContentType = Request.GetHeader("Content-Type");
				if (ContentType && !strncmp(ContentType, "multipart/form-data", 19))
				{
					Response.Data = Content::Wants_Save;
					if (Callback)
						Callback(this, NetEvent::Finished, nullptr, 0);

					return true;
				}

				const char* TransferEncoding = Request.GetHeader("Transfer-Encoding");
				if (TransferEncoding && !Core::Parser::CaseCompare(TransferEncoding, "chunked"))
				{
					Parser* Parser = new HTTP::Parser();
					return Stream->ReadAsync((int64_t)Root->Router->PayloadMaxLength, [this, Parser, Callback](NetEvent Event, const char* Buffer, size_t Recv)
					{
						if (Packet::IsData(Event))
						{
							int64_t Result = Parser->ParseDecodeChunked((char*)Buffer, &Recv);
							if (Result == -1)
							{
								TH_RELEASE(Parser);
								Response.Data = Content::Corrupted;

								if (Callback)
									Callback(this, NetEvent::Timeout, nullptr, 0);

								return false;
							}
							else if (Result >= 0 || Result == -2)
							{
								if (Callback)
									Callback(this, NetEvent::Packet, Buffer, Recv);

								if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
									Request.Buffer.append(Buffer, Recv);
							}

							return Result == -2;
						}
						else if (Packet::IsDone(Event))
						{
							if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
								Response.Data = Content::Cached;
							else
								Response.Data = Content::Lost;

							if (Callback)
								Callback(this, NetEvent::Finished, nullptr, 0);
						}
						else if (Packet::IsErrorOrSkip(Event))
						{
							Response.Data = Content::Corrupted;
							if (Callback)
								Callback(this, Event, nullptr, 0);
						}

						TH_RELEASE(Parser);
						return true;
					}) > 0;
				}
				else if (!Request.GetHeader("Content-Length"))
				{
					return Stream->ReadAsync((int64_t)Root->Router->PayloadMaxLength, [this, Callback](NetEvent Event, const char* Buffer, size_t Recv)
					{
						if (Packet::IsData(Event))
						{
							if (Callback)
								Callback(this, NetEvent::Packet, Buffer, Recv);

							if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
								Request.Buffer.append(Buffer, Recv);
						}
						else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
						{
							if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
								Response.Data = Content::Cached;
							else
								Response.Data = Content::Lost;

							if (Callback)
								Callback(this, Event, nullptr, 0);

							return false;
						}

						return true;
					}) > 0;
				}

				if (Request.ContentLength > Root->Router->PayloadMaxLength)
				{
					Response.Data = Content::Payload_Exceeded;
					if (Callback)
						Callback(this, NetEvent::Timeout, nullptr, 0);

					return false;
				}

				if (!Route || Request.ContentLength > Route->MaxCacheLength)
				{
					Response.Data = Content::Wants_Save;
					if (Callback)
						Callback(this, NetEvent::Timeout, nullptr, 0);

					return true;
				}

				return Stream->ReadAsync((int64_t)Request.ContentLength, [this, Callback](NetEvent Event, const char* Buffer, size_t Recv)
				{
					if (Packet::IsData(Event))
					{
						if (Callback)
							Callback(this, NetEvent::Packet, Buffer, Recv);

						if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
							Request.Buffer.append(Buffer, Recv);
					}
					else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
					{
						if (!Route || Request.Buffer.size() < Route->MaxCacheLength)
							Response.Data = Content::Cached;
						else
							Response.Data = Content::Lost;

						if (Callback)
							Callback(this, Event, nullptr, 0);

						return false;
					}

					return true;
				}) > 0;
			}
			bool Connection::Store(const ResourceCallback& Callback)
			{
				if (!Route || Response.Data == Content::Lost || Response.Data == Content::Empty || Response.Data == Content::Cached || Response.Data == Content::Corrupted || Response.Data == Content::Payload_Exceeded || Response.Data == Content::Save_Exception)
				{
					if (Callback)
						Callback(this, nullptr);

					return false;
				}

				if (Response.Data == Content::Saved)
				{
					if (!Callback || Request.Resources.empty())
						return true;

					for (auto& Item : Request.Resources)
						Callback(this, &Item);

					Callback(this, nullptr);
					return true;
				}

				const char* ContentType = Request.GetHeader("Content-Type"), * BoundaryName;
				if (ContentType && !strncmp(ContentType, "multipart/form-data", 19))
					Response.Data = Content::Wants_Save;

				if (ContentType != nullptr && (BoundaryName = strstr(ContentType, "boundary=")))
				{
					std::string Boundary("--");
					Boundary.append(BoundaryName + 9);

					ParserFrame* Segment = TH_NEW(ParserFrame);
					Segment->Route = Route;
					Segment->Request = &Request;
					Segment->Response = &Response;
					Segment->Callback = Callback;

					Parser* Parser = new HTTP::Parser();
					Parser->OnContentData = Util::ParseMultipartContentData;
					Parser->OnHeaderField = Util::ParseMultipartHeaderField;
					Parser->OnHeaderValue = Util::ParseMultipartHeaderValue;
					Parser->OnResourceBegin = Util::ParseMultipartResourceBegin;
					Parser->OnResourceEnd = Util::ParseMultipartResourceEnd;
					Parser->UserPointer = Segment;

					return Stream->ReadAsync((int64_t)Request.ContentLength, [this, Parser, Segment, Boundary](NetEvent Event, const char* Buffer, size_t Recv)
					{
						if (Packet::IsData(Event))
						{
							if (Parser->MultipartParse(Boundary.c_str(), Buffer, Recv) != -1 && !Segment->Close)
								return true;

							Response.Data = Content::Saved;
							if (Segment->Callback)
								Segment->Callback(this, nullptr);

							TH_RELEASE(Parser);
							TH_DELETE(ParserFrame, Segment);

							return false;
						}
						else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
						{
							if (Packet::IsError(Event))
								Response.Data = Content::Corrupted;
							else
								Response.Data = Content::Saved;

							if (Segment->Callback)
								Segment->Callback(this, nullptr);

							TH_RELEASE(Parser);
							TH_DELETE(ParserFrame, Segment);
						}

						return true;
					}) > 0;
				}
				else if (Request.ContentLength > 0)
				{
					HTTP::Resource fResource;
					fResource.Length = Request.ContentLength;
					fResource.Type = (ContentType ? ContentType : "application/octet-stream");
					fResource.Path = Core::OS::Directory::Get() + Compute::Common::MD5Hash(Compute::Common::RandomBytes(16));

					FILE* File = (FILE*)Core::OS::File::Open(fResource.Path.c_str(), "wb");
					if (!File)
					{
						Response.Data = Content::Save_Exception;
						return false;
					}

					return Stream->ReadAsync((int64_t)Request.ContentLength, [this, File, fResource, Callback](NetEvent Event, const char* Buffer, size_t Recv)
					{
						if (Packet::IsData(Event))
						{
							if (fwrite(Buffer, 1, Recv, File) == Recv)
								return true;

							Response.Data = Content::Save_Exception;
							TH_CLOSE(File);

							if (Callback)
								Callback(this, nullptr);

							return false;
						}
						else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
						{
							if (Packet::IsDone(Event))
							{
								Response.Data = Content::Saved;
								Request.Resources.push_back(fResource);

								if (Callback)
									Callback(this, &Request.Resources.back());
							}
							else
								Response.Data = Content::Corrupted;

							if (Callback)
								Callback(this, nullptr);

							TH_CLOSE(File);
							return false;
						}

						return true;
					}) > 0;
				}
				else if (Callback)
					Callback(this, nullptr);

				return true;
			}
			bool Connection::Finish()
			{
				Info.Sync.lock();
				if (WebSocket != nullptr)
				{
					if (!WebSocket->IsFinished())
					{
						Info.Sync.unlock();
						WebSocket->Finish();
						return false;
					}

					TH_DELETE(WebSocketFrame, WebSocket);
					WebSocket = nullptr;
				}

				if (Gateway != nullptr)
				{
					if (!Gateway->IsFinished())
					{
						Info.Sync.unlock();
						Gateway->Finish();
						return false;
					}

					TH_DELETE(GatewayFrame, Gateway);
					Gateway = nullptr;
				}

				Info.Sync.unlock();
				if (Response.StatusCode < 0 || Stream->Outcome > 0 || !Stream->IsValid())
					return Root->Manage(this);

				if (Response.StatusCode >= 400 && !Response.Error && Response.Buffer.empty())
				{
					Response.Error = true;
					if (Route != nullptr)
					{
						for (auto& Page : Route->ErrorFiles)
						{
							if (Page.StatusCode != Response.StatusCode && Page.StatusCode != 0)
								continue;

							Request.Path = Page.Pattern;
							Response.SetHeader("X-Error", Info.Message);
							return Util::RouteGET(this);
						}
					}

					const char* StatusText = Util::StatusMessage(Response.StatusCode);
					Core::Parser Content;
					Content.fAppend("%s %d %s\r\n", Request.Version, Response.StatusCode, StatusText);

					Util::ConstructHeadUncache(this, &Content);
					if (Route && Route->Callbacks.Headers)
						Route->Callbacks.Headers(this, nullptr);

					char Date[64];
					Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Info.Start / 1000);

					std::string Auth;
					if (Route && Request.User.Type == Auth::Denied)
						Auth = "WWW-Authenticate: " + Route->Auth.Type + " realm=\"" + Route->Auth.Realm + "\"\r\n";

					int HasContents = (Response.StatusCode > 199 && Response.StatusCode != 204 && Response.StatusCode != 304);
					if (HasContents)
					{
						char Buffer[8192];
						std::string Reason = TextHTML(Info.Message);
						snprintf(Buffer, sizeof(Buffer), "<html><head><title>%d %s</title><style>html{font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;height:95%%;}body{display:flex;align-items:center;justify-content:center;height:100%%;}%s</style></head><body><div><h1>%d %s</h1></div></body></html>\n", Response.StatusCode, StatusText, Reason.size() <= 128 ? "div{text-align:center;}" : "h1{font-size:16px;font-weight:normal;}", Response.StatusCode, Reason.empty() ? StatusText : Reason.c_str());

						if (Route && Route->Callbacks.Headers)
							Route->Callbacks.Headers(this, &Content);

						Content.fAppend("Date: %s\r\n%sContent-Type: text/html; charset=%s\r\nAccept-Ranges: bytes\r\nContent-Length: %llu\r\n%s\r\n%s", Date, Util::ConnectionResolve(this).c_str(), Route ? Route->CharSet.c_str() : "utf-8", (uint64_t)strlen(Buffer), Auth.c_str(), Buffer);
					}
					else
					{
						if (Route && Route->Callbacks.Headers)
							Route->Callbacks.Headers(this, &Content);

						Content.fAppend("Date: %s\r\nAccept-Ranges: bytes\r\n%s%s\r\n", Date, Util::ConnectionResolve(this).c_str(), Auth.c_str());
					}

					return Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [this](NetEvent Event, size_t Sent)
					{
						if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
							Root->Manage(this);
					});
				}

				Core::Parser Content;
				std::string Boundary;
				const char* ContentType;
				Content.fAppend("%s %d %s\r\n", Request.Version, Response.StatusCode, Util::StatusMessage(Response.StatusCode));

				if (!Response.GetHeader("Date"))
				{
					char Buffer[64];
					Core::DateTime::TimeFormatGMT(Buffer, sizeof(Buffer), Info.Start / 1000);
					Content.fAppend("Date: %s\r\n", Buffer);
				}

				if (!Response.GetHeader("Connection"))
					Content.Append(Util::ConnectionResolve(this));

				if (!Response.GetHeader("Accept-Ranges"))
					Content.Append("Accept-Ranges: bytes\r\n", 22);

				if (!Response.GetHeader("Content-Type"))
				{
					if (Route != nullptr)
						ContentType = Util::ContentType(Request.Path, &Route->MimeTypes);
					else
						ContentType = "application/octet-stream";

					if (Request.GetHeader("Range") != nullptr)
					{
						Boundary = Util::ParseMultipartDataBoundary();
						Content.fAppend("Content-Type: multipart/byteranges; boundary=%s; charset=%s\r\n", ContentType, Boundary.c_str(), Route ? Route->CharSet.c_str() : "utf-8");
					}
					else
						Content.fAppend("Content-Type: %s; charset=%s\r\n", ContentType, Route ? Route->CharSet.c_str() : "utf-8");
				}
				else
					ContentType = Response.GetHeader("Content-Type");

				if (!Response.Buffer.empty())
				{
#ifdef TH_HAS_ZLIB
					bool Deflate = false, Gzip = false;
					if (Util::ResourceCompressed(this, Response.Buffer.size()))
					{
						const char* AcceptEncoding = Request.GetHeader("Accept-Encoding");
						if (AcceptEncoding != nullptr)
						{
							Deflate = strstr(AcceptEncoding, "deflate") != nullptr;
							Gzip = strstr(AcceptEncoding, "gzip") != nullptr;
						}

						if (AcceptEncoding != nullptr && (Deflate || Gzip))
						{
							z_stream fStream;
							fStream.zalloc = Z_NULL;
							fStream.zfree = Z_NULL;
							fStream.opaque = Z_NULL;
							fStream.avail_in = (uInt)Response.Buffer.size();
							fStream.next_in = (Bytef*)Response.Buffer.data();

							if (deflateInit2(&fStream, Route ? Route->Compression.QualityLevel : 8, Z_DEFLATED, (Gzip ? 15 | 16 : 15), Route ? Route->Compression.MemoryLevel : 8, Route ? (int)Route->Compression.Tune : 0) == Z_OK)
							{
								std::string Buffer(Response.Buffer.size(), '\0');
								fStream.avail_out = (uInt)Buffer.size();
								fStream.next_out = (Bytef*)Buffer.c_str();
								bool Compress = (deflate(&fStream, Z_FINISH) == Z_STREAM_END);
								bool Flush = (deflateEnd(&fStream) == Z_OK);

								if (Compress && Flush)
								{
									TextAssign(Response.Buffer, Buffer.c_str(), (uint64_t)fStream.total_out);
									if (!Response.GetHeader("Content-Encoding"))
									{
										if (Gzip)
											Content.Append("Content-Encoding: gzip\r\n", 24);
										else
											Content.Append("Content-Encoding: deflate\r\n", 27);
									}
								}
							}
						}
					}
#endif
					if (Request.GetHeader("Range") != nullptr)
					{
						std::vector<std::pair<int64_t, int64_t>> Ranges = Request.GetRanges();
						if (Ranges.size() > 1)
						{
							std::string Data;
							for (auto It = Ranges.begin(); It != Ranges.end(); ++It)
							{
								std::pair<uint64_t, uint64_t> Offset = Request.GetRange(It, Response.Buffer.size());
								std::string ContentRange = Util::ConstructContentRange(Offset.first, Offset.second, Response.Buffer.size());

								Data.append("--", 2);
								Data.append(Boundary);
								Data.append("\r\n", 2);

								if (ContentType != nullptr)
								{
									Data.append("Content-Type: ", 14);
									Data.append(ContentType);
									Data.append("\r\n", 2);
								}

								Data.append("Content-Range: ", 15);
								Data.append(ContentRange.c_str(), ContentRange.size());
								Data.append("\r\n", 2);
								Data.append("\r\n", 2);
								Data.append(TextSubstring(Response.Buffer, Offset.first, Offset.second));
								Data.append("\r\n", 2);
							}

							Data.append("--", 2);
							Data.append(Boundary);
							Data.append("--\r\n", 4);

							TextAssign(Response.Buffer, Data);
						}
						else
						{
							std::pair<uint64_t, uint64_t> Offset = Request.GetRange(Ranges.begin(), Response.Buffer.size());
							if (!Response.GetHeader("Content-Range"))
								Content.fAppend("Content-Range: %s\r\n", Util::ConstructContentRange(Offset.first, Offset.second, Response.Buffer.size()).c_str());

							TextAssign(Response.Buffer, TextSubstring(Response.Buffer, Offset.first, Offset.second));
						}
					}

					if (!Response.GetHeader("Content-Length"))
						Content.fAppend("Content-Length: %llu\r\n", (uint64_t)Response.Buffer.size());
				}
				else if (!Response.GetHeader("Content-Length"))
					Content.Append("Content-Length: 0\r\n", 19);

				Util::ConstructHeadFull(&Request, &Response, false, &Content);
				if (Route && Route->Callbacks.Headers)
					Route->Callbacks.Headers(this, &Content);

				Content.Append("\r\n", 2);
				return Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [this](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						if (memcmp(Request.Method, "HEAD", 4) != 0 && !Response.Buffer.empty())
						{
							Stream->WriteAsync(Response.Buffer.data(), (int64_t)Response.Buffer.size(), [this](NetEvent Event, size_t Sent)
							{
								if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
									Root->Manage(this);
							});
						}
						else
							Root->Manage(this);
					}
					else if (Packet::IsErrorOrSkip(Event))
						Root->Manage(this);
				});
			}
			bool Connection::Finish(int StatusCode)
			{
				Response.StatusCode = StatusCode;
				return Finish();
			}
			bool Connection::Certify(Certificate* Output)
			{
#ifdef TH_HAS_OPENSSL
				TH_ASSERT(Output != nullptr, false, "certificate should be set");
				X509* Certificate = SSL_get_peer_certificate(Stream->GetDevice());
				if (!Certificate)
					return false;

				const EVP_MD* Digest = EVP_get_digestbyname("sha1");
				X509_NAME* Subject = X509_get_subject_name(Certificate);
				X509_NAME* Issuer = X509_get_issuer_name(Certificate);
				ASN1_INTEGER* Serial = X509_get_serialNumber(Certificate);

				char SubjectBuffer[1024];
				X509_NAME_oneline(Subject, SubjectBuffer, (int)sizeof(SubjectBuffer));

				char IssuerBuffer[1024], SerialBuffer[1024];
				X509_NAME_oneline(Issuer, IssuerBuffer, (int)sizeof(IssuerBuffer));

				unsigned char Buffer[256];
				int Length = i2d_ASN1_INTEGER(Serial, nullptr);

				if (Length > 0 && (unsigned)Length < (unsigned)sizeof(Buffer))
				{
					unsigned char* Pointer = Buffer;
					int Size = i2d_ASN1_INTEGER(Serial, &Pointer);

					if (!Compute::Common::HexToString(Buffer, (uint64_t)Size, SerialBuffer, sizeof(SerialBuffer)))
						*SerialBuffer = '\0';
				}
				else
					*SerialBuffer = '\0';

				unsigned int Size = 0;
				ASN1_digest((int(*)(void*, unsigned char**))i2d_X509, Digest, (char*)Certificate, Buffer, &Size);

				char FingerBuffer[1024];
				if (!Compute::Common::HexToString(Buffer, (uint64_t)Size, FingerBuffer, sizeof(FingerBuffer)))
					*FingerBuffer = '\0';

				Output->Subject = SubjectBuffer;
				Output->Issuer = IssuerBuffer;
				Output->Serial = SerialBuffer;
				Output->Finger = FingerBuffer;

				X509_free(Certificate);
				return true;
#else
				return false;
#endif
			}

			QueryParameter::QueryParameter() : Core::Document(Core::Var::Object())
			{
			}
			std::string QueryParameter::Build()
			{
				std::string Output, Label = Compute::Common::URIEncode(Parent != nullptr ? ('[' + Key + ']') : Key);
				if (Value.IsObject())
				{
					for (auto It = Nodes.begin(); It != Nodes.end(); ++It)
					{
						Output.append(Label).append(((QueryParameter*)*It)->Build());
						if (It + 1 < Nodes.end())
							Output += '&';
					}
				}
				else
				{
					std::string V = Value.Serialize();
					if (!V.empty())
						Output.append(Label).append(1, '=').append(Compute::Common::URIEncode(V));
					else
						Output.append(Label);
				}

				return Output;
			}
			std::string QueryParameter::BuildFromBase()
			{
				std::string Output, Label = Compute::Common::URIEncode(Key);
				if (Value.IsObject())
				{
					for (auto It = Nodes.begin(); It != Nodes.end(); ++It)
					{
						Output.append(Label).append(((QueryParameter*)*It)->Build());
						if (It + 1 < Nodes.end())
							Output += '&';
					}
				}
				else
				{
					std::string V = Value.Serialize();
					if (!V.empty())
						Output.append(Label).append(1, '=').append(Compute::Common::URIEncode(V));
					else
						Output.append(Label);
				}

				return Output;
			}
			QueryParameter* QueryParameter::Find(QueryToken* Name)
			{
				TH_ASSERT(Name != nullptr, nullptr, "token should be set");
				if (Name->Value && Name->Length > 0)
				{
					for (auto* Item : Nodes)
					{
						if (!strncmp(Item->Key.c_str(), Name->Value, (size_t)Name->Length))
							return (QueryParameter*)Item;
					}
				}

				QueryParameter* New = new QueryParameter();
				if (Name->Value && Name->Length > 0)
				{
					New->Key.assign(Name->Value, (size_t)Name->Length);
					if (!Core::Parser(&New->Key).HasInteger())
						Value = Core::Var::Object();
					else
						Value = Core::Var::Array();
				}
				else
				{
					New->Key.assign(std::to_string(Nodes.size()));
					Value = Core::Var::Array();
				}

				New->Value = Core::Var::String("", 0);
				New->Parent = this;
				Nodes.push_back(New);

				return New;
			}

			Query::Query() : Object(new QueryParameter())
			{
			}
			Query::~Query()
			{
				TH_RELEASE(Object);
			}
			void Query::Clear()
			{
				if (Object != nullptr)
					Object->Clear();
			}
			void Query::Steal(Core::Document** Output)
			{
				if (!Output)
					return;

				TH_RELEASE(*Output);
				*Output = Object;
				Object = nullptr;
			}
			void Query::NewParameter(std::vector<QueryToken>* Tokens, const QueryToken& Name, const QueryToken& Value)
			{
				std::string URI = Compute::Common::URIDecode(Name.Value, Name.Length);
				char* Data = (char*)URI.c_str();

				uint64_t Offset = 0, Length = URI.size();
				for (uint64_t i = 0; i < Length; i++)
				{
					if (Data[i] != '[')
					{
						if (Tokens->empty() && i + 1 >= Length)
						{
							QueryToken Token;
							Token.Value = Data + Offset;
							Token.Length = i - Offset + 1;
							Tokens->push_back(Token);
						}

						continue;
					}

					if (Tokens->empty())
					{
						QueryToken Token;
						Token.Value = Data + Offset;
						Token.Length = i - Offset;
						Tokens->push_back(Token);
					}

					Offset = i;
					while (i + 1 < Length && Data[i + 1] != ']')
						i++;

					QueryToken Token;
					Token.Value = Data + Offset + 1;
					Token.Length = i - Offset;
					Tokens->push_back(Token);

					if (i + 1 >= Length)
						break;

					Offset = i + 1;
				}

				if (!Value.Value || !Value.Length)
					return;

				QueryParameter* Parameter = nullptr;
				for (auto& Item : *Tokens)
				{
					if (Parameter != nullptr)
						Parameter = Parameter->Find(&Item);
					else
						Parameter = GetParameter(&Item);
				}

				if (Parameter != nullptr)
					Parameter->Value.Deserialize(Compute::Common::URIDecode(Value.Value, Value.Length));
			}
			void Query::Decode(const char* Type, const std::string& URI)
			{
				if (!Type || URI.empty())
					return;

				if (!Core::Parser::CaseCompare(Type, "application/x-www-form-urlencoded"))
					DecodeAXWFD(URI);
				else if (!Core::Parser::CaseCompare(Type, "application/json"))
					DecodeAJSON(URI);
			}
			void Query::DecodeAXWFD(const std::string& URI)
			{
				std::vector<QueryToken> Tokens;
				char* Data = (char*)URI.c_str();

				uint64_t Offset = 0, Length = URI.size();
				for (uint64_t i = 0; i < Length; i++)
				{
					if (Data[i] != '&' && Data[i] != '=' && i + 1 < Length)
						continue;

					QueryToken Name;
					Name.Value = Data + Offset;
					Name.Length = i - Offset;
					Offset = i;

					if (Data[i] == '=')
					{
						while (i + 1 < Length && Data[i + 1] != '&')
							i++;
					}

					QueryToken Value;
					Value.Value = Data + Offset + 1;
					Value.Length = i - Offset;

					NewParameter(&Tokens, Name, Value);
					Tokens.clear();
					Offset = i + 2;
					i++;
				}
			}
			void Query::DecodeAJSON(const std::string& URI)
			{
				size_t Offset = 0;
				TH_CLEAR(Object);

				Object = (QueryParameter*)Core::Document::ReadJSON(URI.size(), [&URI, &Offset](char* Buffer, int64_t Size)
				{
					if (!Buffer || !Size)
						return true;

					size_t Length = URI.size() - Offset;
					if (Size < Length)
						Length = Size;

					if (!Length)
						return false;

					memcpy(Buffer, URI.c_str() + Offset, Length);
					Offset += Length;
					return true;
				});
			}
			std::string Query::Encode(const char* Type)
			{
				if (Type != nullptr)
				{
					if (!Core::Parser::CaseCompare(Type, "application/x-www-form-urlencoded"))
						return EncodeAXWFD();

					if (!Core::Parser::CaseCompare(Type, "application/json"))
						return EncodeAJSON();
				}

				return "";
			}
			std::string Query::EncodeAXWFD()
			{
				std::string Output; auto& Nodes = Object->GetChilds();
				for (auto It = Nodes.begin(); It != Nodes.end(); ++It)
				{
					if (It + 1 < Nodes.end())
						Output.append(((QueryParameter*)*It)->BuildFromBase()).append(1, '&');
					else
						Output.append(((QueryParameter*)*It)->BuildFromBase());
				}

				return Output;
			}
			std::string Query::EncodeAJSON()
			{
				std::string Stream;
				Core::Document::WriteJSON(Object, [&Stream](Core::VarForm, const char* Buffer, int64_t Length)
				{
					if (Buffer != nullptr && Length > 0)
						Stream.append(Buffer, Length);
				});

				return Stream;
			}
			QueryParameter* Query::Get(const char* Name)
			{
				return (QueryParameter*)Object->Get(Name);
			}
			QueryParameter* Query::Set(const char* Name)
			{
				return (QueryParameter*)Object->Set(Name, Core::Var::String("", 0));
			}
			QueryParameter* Query::Set(const char* Name, const char* Value)
			{
				return (QueryParameter*)Object->Set(Name, Core::Var::String(Value));
			}
			QueryParameter* Query::GetParameter(QueryToken* Name)
			{
				TH_ASSERT(Name != nullptr, nullptr, "token should be set");
				if (Name->Value && Name->Length > 0)
				{
					for (auto* Item : Object->GetChilds())
					{
						if (Item->Key.size() != Name->Length)
							continue;

						if (!strncmp(Item->Key.c_str(), Name->Value, (size_t)Name->Length))
							return (QueryParameter*)Item;
					}
				}

				QueryParameter* New = new QueryParameter();
				if (Name->Value && Name->Length > 0)
				{
					New->Key.assign(Name->Value, (size_t)Name->Length);
					if (!Core::Parser(&New->Key).HasInteger())
						Object->Value = Core::Var::Object();
					else
						Object->Value = Core::Var::Array();
				}
				else
				{
					New->Key.assign(std::to_string(Object->Size()));
					Object->Value = Core::Var::Array();
				}

				New->Value = Core::Var::String("", 0);
				Object->Push(New);

				return New;
			}

			Session::Session()
			{
				Query = Core::Var::Set::Object();
			}
			Session::~Session()
			{
				TH_RELEASE(Query);
			}
			void Session::Clear()
			{
				if (Query != nullptr)
					Query->Clear();
			}
			bool Session::Write(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				std::string Document = Base->Route->Site->Gateway.Session.DocumentRoot + FindSessionId(Base);

				FILE* Stream = (FILE*)Core::OS::File::Open(Document.c_str(), "wb");
				if (!Stream)
					return false;

				SessionExpires = time(nullptr) + Base->Route->Site->Gateway.Session.Expires;
				fwrite(&SessionExpires, sizeof(int64_t), 1, Stream);

				Query->WriteJSONB(Query, [Stream](Core::VarForm, const char* Buffer, int64_t Size)
				{
					if (Buffer != nullptr && Size > 0)
						fwrite(Buffer, Size, 1, Stream);
				});

				TH_CLOSE(Stream);
				return true;
			}
			bool Session::Read(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				std::string Document = Base->Route->Site->Gateway.Session.DocumentRoot + FindSessionId(Base);

				FILE* Stream = (FILE*)Core::OS::File::Open(Document.c_str(), "rb");
				if (!Stream)
					return false;

				fseek(Stream, 0, SEEK_END);
				if (ftell(Stream) == 0)
				{
					TH_CLOSE(Stream);
					return false;
				}

				fseek(Stream, 0, SEEK_SET);
				if (fread(&SessionExpires, 1, sizeof(int64_t), Stream) != sizeof(int64_t))
				{
					TH_CLOSE(Stream);
					return false;
				}

				if (SessionExpires <= time(nullptr))
				{
					SessionId.clear();
					TH_CLOSE(Stream);

					if (!Core::OS::File::Remove(Document.c_str()))
						TH_ERR("session file %s cannot be deleted", Document.c_str());

					return false;
				}


				Core::Document* V = Core::Document::ReadJSONB([Stream](char* Buffer, int64_t Size)
				{
					if (!Buffer || !Size)
						return true;

					return fread(Buffer, sizeof(char), Size, Stream) == Size;
				});

				if (V != nullptr)
				{
					TH_RELEASE(Query);
					Query = V;
				}

				TH_CLOSE(Stream);
				return true;
			}
			std::string& Session::FindSessionId(Connection* Base)
			{
				if (!SessionId.empty())
					return SessionId;

				TH_ASSERT(Base != nullptr && Base->Route != nullptr, SessionId, "connection should be set");
				const char* Value = Base->Request.GetCookie(Base->Route->Site->Gateway.Session.Name.c_str());
				if (!Value)
					return GenerateSessionId(Base);

				return SessionId.assign(Value);
			}
			std::string& Session::GenerateSessionId(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, SessionId, "connection should be set");
				int64_t Time = time(nullptr);
				SessionId = Compute::Common::MD5Hash(Base->Request.URI + std::to_string(Time));
				IsNewSession = true;

				if (SessionExpires == 0)
					SessionExpires = Time + Base->Route->Site->Gateway.Session.Expires;

				Base->Response.SetCookie(Base->Route->Site->Gateway.Session.Name.c_str(), SessionId.c_str(), Time + (int64_t)Base->Route->Site->Gateway.Session.CookieExpires, Base->Route->Site->Gateway.Session.Domain.c_str(), Base->Route->Site->Gateway.Session.Path.c_str(), false);
				return SessionId;
			}
			bool Session::InvalidateCache(const std::string& Path)
			{
				std::vector<Core::ResourceEntry> Entries;
				if (!Core::OS::Directory::Scan(Path, &Entries))
					return false;

				bool Split = (Path.back() != '\\' && Path.back() != '/');
				for (auto& Item : Entries)
				{
					if (Item.Source.IsDirectory)
						continue;

					std::string Filename = (Split ? Path + '/' : Path) + Item.Path;
					if (!Core::OS::File::Remove(Filename.c_str()))
						TH_ERR("couldn't invalidate session\n\t%s", Item.Path.c_str());
				}

				return true;
			}

			Parser::Parser()
			{
			}
			Parser::~Parser()
			{
				TH_FREE(Multipart.Boundary);
			}
			int64_t Parser::MultipartParse(const char* Boundary, const char* Buffer, size_t Length)
			{
				TH_ASSERT(Buffer != nullptr, -1, "buffer should be set");
				TH_ASSERT(Boundary != nullptr, -1, "boundary should be set");

				if (!Multipart.Boundary || !Multipart.LookBehind)
				{
					if (Multipart.Boundary)
						TH_FREE(Multipart.Boundary);

					Multipart.Length = strlen(Boundary);
					Multipart.Boundary = (char*)TH_MALLOC(sizeof(char) * (size_t)(Multipart.Length * 2 + 9));
					memcpy(Multipart.Boundary, Boundary, sizeof(char) * (size_t)Multipart.Length);
					Multipart.Boundary[Multipart.Length] = '\0';
					Multipart.LookBehind = (Multipart.Boundary + Multipart.Length + 1);
					Multipart.Index = 0;
					Multipart.State = MultipartState_Start;
				}

				char Value, Lower;
				char LF = 10, CR = 13;
				size_t i = 0, Mark = 0;
				int Last = 0;

				while (i < Length)
				{
					Value = Buffer[i];
					Last = (i == (Length - 1));

					switch (Multipart.State)
					{
						case MultipartState_Start:
							Multipart.Index = 0;
							Multipart.State = MultipartState_Start_Boundary;
						case MultipartState_Start_Boundary:
							if (Multipart.Index == Multipart.Length)
							{
								if (Value != CR)
									return i;

								Multipart.Index++;
								break;
							}
							else if (Multipart.Index == (Multipart.Length + 1))
							{
								if (Value != LF)
									return i;

								Multipart.Index = 0;
								if (OnResourceBegin && !OnResourceBegin(this))
									return i;

								Multipart.State = MultipartState_Header_Field_Start;
								break;
							}

							if (Value != Multipart.Boundary[Multipart.Index])
								return i;

							Multipart.Index++;
							break;
						case MultipartState_Header_Field_Start:
							Mark = i;
							Multipart.State = MultipartState_Header_Field;
						case MultipartState_Header_Field:
							if (Value == CR)
							{
								Multipart.State = MultipartState_Header_Field_Waiting;
								break;
							}

							if (Value == ':')
							{
								if (OnHeaderField && !OnHeaderField(this, Buffer + Mark, i - Mark))
									return i;

								Multipart.State = MultipartState_Header_Value_Start;
								break;
							}

							Lower = tolower(Value);
							if ((Value != '-') && (Lower < 'a' || Lower > 'z'))
								return i;

							if (Last && OnHeaderField && !OnHeaderField(this, Buffer + Mark, (i - Mark) + 1))
								return i;

							break;
						case MultipartState_Header_Field_Waiting:
							if (Value != LF)
								return i;

							Multipart.State = MultipartState_Resource_Start;
							break;
						case MultipartState_Header_Value_Start:
							if (Value == ' ')
								break;

							Mark = i;
							Multipart.State = MultipartState_Header_Value;
						case MultipartState_Header_Value:
							if (Value == CR)
							{
								if (OnHeaderValue && !OnHeaderValue(this, Buffer + Mark, i - Mark))
									return i;

								Multipart.State = MultipartState_Header_Value_Waiting;
								break;
							}

							if (Last && OnHeaderValue && !OnHeaderValue(this, Buffer + Mark, (i - Mark) + 1))
								return i;

							break;
						case MultipartState_Header_Value_Waiting:
							if (Value != LF)
								return i;

							Multipart.State = MultipartState_Header_Field_Start;
							break;
						case MultipartState_Resource_Start:
							Mark = i;
							Multipart.State = MultipartState_Resource;
						case MultipartState_Resource:
							if (Value == CR)
							{
								if (OnContentData && !OnContentData(this, Buffer + Mark, i - Mark))
									return i;

								Mark = i;
								Multipart.State = MultipartState_Resource_Boundary_Waiting;
								Multipart.LookBehind[0] = CR;
								break;
							}

							if (Last && OnContentData && !OnContentData(this, Buffer + Mark, (i - Mark) + 1))
								return i;

							break;
						case MultipartState_Resource_Boundary_Waiting:
							if (Value == LF)
							{
								Multipart.State = MultipartState_Resource_Boundary;
								Multipart.LookBehind[1] = LF;
								Multipart.Index = 0;
								break;
							}

							if (OnContentData && !OnContentData(this, Multipart.LookBehind, 1))
								return i;

							Multipart.State = MultipartState_Resource;
							Mark = i--;
							break;
						case MultipartState_Resource_Boundary:
							if (Multipart.Boundary[Multipart.Index] != Value)
							{
								if (OnContentData && !OnContentData(this, Multipart.LookBehind, 2 + Multipart.Index))
									return i;

								Multipart.State = MultipartState_Resource;
								Mark = i--;
								break;
							}

							Multipart.LookBehind[2 + Multipart.Index] = Value;
							if ((++Multipart.Index) == Multipart.Length)
							{
								if (OnResourceEnd && !OnResourceEnd(this))
									return i;

								Multipart.State = MultipartState_Resource_Waiting;
							}
							break;
						case MultipartState_Resource_Waiting:
							if (Value == '-')
							{
								Multipart.State = MultipartState_Resource_Hyphen;
								break;
							}

							if (Value == CR)
							{
								Multipart.State = MultipartState_Resource_End;
								break;
							}

							return i;
						case MultipartState_Resource_Hyphen:
							if (Value == '-')
							{
								Multipart.State = MultipartState_End;
								break;
							}

							return i;
						case MultipartState_Resource_End:
							if (Value == LF)
							{
								Multipart.State = MultipartState_Header_Field_Start;
								if (OnResourceBegin && !OnResourceBegin(this))
									return i;

								break;
							}

							return i;
						case MultipartState_End:
							break;
						default:
							return -1;
					}

					++i;
				}

				return Length;
			}
			int64_t Parser::ParseRequest(const char* BufferStart, size_t Length, size_t LastLength)
			{
				TH_ASSERT(BufferStart != nullptr, -1, "buffer start should be set");
				const char* Buffer = BufferStart;
				const char* BufferEnd = BufferStart + Length;
				int Result;

				if (LastLength != 0 && Complete(Buffer, BufferEnd, LastLength, &Result) == nullptr)
					return (int64_t)Result;

				if ((Buffer = ProcessRequest(Buffer, BufferEnd, &Result)) == nullptr)
					return (int64_t)Result;

				return (int64_t)(Buffer - BufferStart);
			}
			int64_t Parser::ParseResponse(const char* BufferStart, size_t Length, size_t LastLength)
			{
				TH_ASSERT(BufferStart != nullptr, -1, "buffer start should be set");
				const char* Buffer = BufferStart;
				const char* BufferEnd = Buffer + Length;
				int Result;

				if (LastLength != 0 && Complete(Buffer, BufferEnd, LastLength, &Result) == nullptr)
					return (int64_t)Result;

				if ((Buffer = ProcessResponse(Buffer, BufferEnd, &Result)) == nullptr)
					return (int64_t)Result;

				return (int64_t)(Buffer - BufferStart);
			}
			int64_t Parser::ParseDecodeChunked(char* Buffer, size_t* Length)
			{
				TH_ASSERT(Buffer != nullptr && Length != nullptr, -1, "buffer should be set");
				size_t Dest = 0, Src = 0, Size = *Length;
				int64_t Result = -2;

				while (true)
				{
					switch (Chunked.State)
					{
						case ChunkedState_Size:
							for (;; ++Src)
							{
								if (Src == Size)
									goto Exit;

								int V = Buffer[Src];
								if ('0' <= V && V <= '9')
									V = V - '0';
								else if ('A' <= V && V <= 'F')
									V = V - 'A' + 0xa;
								else if ('a' <= V && V <= 'f')
									V = V - 'a' + 0xa;
								else
									V = -1;

								if (V == -1)
								{
									if (Chunked.HexCount == 0)
									{
										Result = -1;
										goto Exit;
									}
									break;
								}

								if (Chunked.HexCount == sizeof(size_t) * 2)
								{
									Result = -1;
									goto Exit;
								}

								Chunked.Length = Chunked.Length * 16 + V;
								++Chunked.HexCount;
							}

							Chunked.HexCount = 0;
							Chunked.State = ChunkedState_Ext;
						case ChunkedState_Ext:
							for (;; ++Src)
							{
								if (Src == Size)
									goto Exit;
								if (Buffer[Src] == '\012')
									break;
							}

							++Src;
							if (Chunked.Length == 0)
							{
								if (Chunked.ConsumeTrailer)
								{
									Chunked.State = ChunkedState_Head;
									break;
								}
								else
									goto Complete;
							}

							Chunked.State = ChunkedState_Data;
						case ChunkedState_Data:
						{
							size_t avail = Size - Src;
							if (avail < Chunked.Length)
							{
								if (Dest != Src)
									memmove(Buffer + Dest, Buffer + Src, avail);

								Src += avail;
								Dest += avail;
								Chunked.Length -= avail;
								goto Exit;
							}

							if (Dest != Src)
								memmove(Buffer + Dest, Buffer + Src, Chunked.Length);

							Src += Chunked.Length;
							Dest += Chunked.Length;
							Chunked.Length = 0;
							Chunked.State = ChunkedState_End;
						}
						case ChunkedState_End:
							for (;; ++Src)
							{
								if (Src == Size)
									goto Exit;

								if (Buffer[Src] != '\015')
									break;
							}

							if (Buffer[Src] != '\012')
							{
								Result = -1;
								goto Exit;
							}

							++Src;
							Chunked.State = ChunkedState_Size;
							break;
						case ChunkedState_Head:
							for (;; ++Src)
							{
								if (Src == Size)
									goto Exit;

								if (Buffer[Src] != '\015')
									break;
							}

							if (Buffer[Src++] == '\012')
								goto Complete;

							Chunked.State = ChunkedState_Middle;
						case ChunkedState_Middle:
							for (;; ++Src)
							{
								if (Src == Size)
									goto Exit;

								if (Buffer[Src] == '\012')
									break;
							}

							++Src;
							Chunked.State = ChunkedState_Head;
							break;
						default:
							return -1;
					}
				}

			Complete:
				Result = Size - Src;

			Exit:
				if (Dest != Src)
					memmove(Buffer + Dest, Buffer + Src, Size - Src);

				*Length = Dest;
				return Result;
			}
			const char* Parser::Tokenize(const char* Buffer, const char* BufferEnd, const char** Token, size_t* TokenLength, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Token != nullptr, nullptr, "token should be set");
				TH_ASSERT(TokenLength != nullptr, nullptr, "token length should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				const char* TokenStart = Buffer;
				while (BufferEnd - Buffer >= 8)
				{
					for (int i = 0; i < 8; i++)
					{
						if (!((unsigned char)(*Buffer) - 040u < 0137u))
							goto NonPrintable;
						++Buffer;
					}

					continue;
				NonPrintable:
					if (((unsigned char)*Buffer < '\040' && *Buffer != '\011') || *Buffer == '\177')
						goto FoundControl;
					++Buffer;
				}

				for (;; ++Buffer)
				{
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (!((unsigned char)(*Buffer) - 040u < 0137u))
					{
						if (((unsigned char)*Buffer < '\040' && *Buffer != '\011') || *Buffer == '\177')
							goto FoundControl;
					}
				}

			FoundControl:
				if (*Buffer == '\015')
				{
					++Buffer;
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (*Buffer++ != '\012')
					{
						*Out = -1;
						return nullptr;
					}

					*TokenLength = Buffer - 2 - TokenStart;
				}
				else if (*Buffer == '\012')
				{
					*TokenLength = Buffer - TokenStart;
					++Buffer;
				}
				else
				{
					*Out = -1;
					return nullptr;
				}

				*Token = TokenStart;
				return Buffer;
			}
			const char* Parser::Complete(const char* Buffer, const char* BufferEnd, size_t LastLength, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				int Result = 0;
				Buffer = LastLength < 3 ? Buffer : Buffer + LastLength - 3;

				while (true)
				{
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (*Buffer == '\015')
					{
						++Buffer;
						if (Buffer == BufferEnd)
						{
							*Out = -2;
							return nullptr;
						}

						if (*Buffer++ != '\012')
						{
							*Out = -1;
							return nullptr;
						}
						++Result;
					}
					else if (*Buffer == '\012')
					{
						++Buffer;
						++Result;
					}
					else
					{
						++Buffer;
						Result = 0;
					}

					if (Result == 2)
						return Buffer;
				}

				return nullptr;
			}
			const char* Parser::ProcessVersion(const char* Buffer, const char* BufferEnd, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				if (BufferEnd - Buffer < 9)
				{
					*Out = -2;
					return nullptr;
				}

				const char* Version = Buffer;
				if (*(Buffer++) != 'H')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != 'T')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != 'T')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != 'P')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != '/')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != '1')
				{
					*Out = -1;
					return nullptr;
				}

				if (*(Buffer++) != '.')
				{
					*Out = -1;
					return nullptr;
				}

				if (*Buffer < '0' || '9' < *Buffer)
				{
					*Out = -1;
					return nullptr;
				}

				Buffer++;
				if (OnVersion && !OnVersion(this, Version, Buffer - Version))
				{
					*Out = -1;
					return nullptr;
				}

				return Buffer;
			}
			const char* Parser::ProcessHeaders(const char* Buffer, const char* BufferEnd, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				static const char* Mapping =
					"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
					"\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
					"\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
					"\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
					"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
					"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
					"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
					"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

				while (true)
				{
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (*Buffer == '\015')
					{
						++Buffer;
						if (Buffer == BufferEnd)
						{
							*Out = -2;
							return nullptr;
						}

						if (*Buffer++ != '\012')
						{
							*Out = -1;
							return nullptr;
						}

						break;
					}
					else if (*Buffer == '\012')
					{
						++Buffer;
						break;
					}

					if (!(*Buffer == ' ' || *Buffer == '\t'))
					{
						const char* Name = Buffer;
						while (true)
						{
							if (*Buffer == ':')
								break;

							if (!Mapping[(unsigned char)*Buffer])
							{
								*Out = -1;
								return nullptr;
							}

							++Buffer;
							if (Buffer == BufferEnd)
							{
								*Out = -2;
								return nullptr;
							}
						}

						int64_t Length = Buffer - Name;
						if (Length == 0)
						{
							*Out = -1;
							return nullptr;
						}

						if (OnHeaderField && !OnHeaderField(this, Name, Length))
						{
							*Out = -1;
							return nullptr;
						}

						++Buffer;
						for (;; ++Buffer)
						{
							if (Buffer == BufferEnd)
							{
								if (OnHeaderValue && !OnHeaderValue(this, "", 0))
								{
									*Out = -1;
									return nullptr;
								}

								*Out = -2;
								return nullptr;
							}

							if (!(*Buffer == ' ' || *Buffer == '\t'))
								break;
						}
					}
					else if (OnHeaderField && !OnHeaderField(this, "", 0))
					{
						*Out = -1;
						return nullptr;
					}

					const char* Value; size_t ValueLength;
					if ((Buffer = Tokenize(Buffer, BufferEnd, &Value, &ValueLength, Out)) == nullptr)
					{
						if (OnHeaderValue)
							OnHeaderValue(this, "", 0);

						return nullptr;
					}

					const char* ValueEnd = Value + ValueLength;
					for (; ValueEnd != Value; --ValueEnd)
					{
						const char c = *(ValueEnd - 1);
						if (!(c == ' ' || c == '\t'))
							break;
					}

					if (OnHeaderValue && !OnHeaderValue(this, Value, ValueEnd - Value))
					{
						*Out = -1;
						return nullptr;
					}
				}

				return Buffer;
			}
			const char* Parser::ProcessRequest(const char* Buffer, const char* BufferEnd, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				if (Buffer == BufferEnd)
				{
					*Out = -2;
					return nullptr;
				}

				if (*Buffer == '\015')
				{
					++Buffer;
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (*Buffer++ != '\012')
					{
						*Out = -1;
						return nullptr;
					}
				}
				else if (*Buffer == '\012')
					++Buffer;

				const char* TokenStart = Buffer;
				if (Buffer == BufferEnd)
				{
					*Out = -2;
					return nullptr;
				}

				while (true)
				{
					if (*Buffer == ' ')
						break;

					if (!((unsigned char)(*Buffer) - 040u < 0137u))
					{
						if ((unsigned char)*Buffer < '\040' || *Buffer == '\177')
						{
							*Out = -1;
							return nullptr;
						}
					}

					++Buffer;
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}
				}

				if (Buffer - TokenStart == 0)
					return nullptr;

				if (OnMethodValue && !OnMethodValue(this, TokenStart, Buffer - TokenStart))
				{
					*Out = -1;
					return nullptr;
				}

				do
				{
					++Buffer;
				} while (*Buffer == ' ');

				TokenStart = Buffer;
				if (Buffer == BufferEnd)
				{
					*Out = -2;
					return nullptr;
				}

				while (true)
				{
					if (*Buffer == ' ')
						break;

					if (!((unsigned char)(*Buffer) - 040u < 0137u))
					{
						if ((unsigned char)*Buffer < '\040' || *Buffer == '\177')
						{
							*Out = -1;
							return nullptr;
						}
					}

					++Buffer;
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}
				}

				if (Buffer - TokenStart == 0)
					return nullptr;

				char* Path = (char*)TokenStart;
				int64_t PL = Buffer - TokenStart, QL = 0;
				while (QL < PL && Path[QL] != '?')
					QL++;

				if (QL > 0 && QL < PL)
				{
					QL = PL - QL - 1;
					PL -= QL + 1;
					if (OnQueryValue && !OnQueryValue(this, Path + PL + 1, QL))
					{
						*Out = -1;
						return nullptr;
					}
				}

				if (OnPathValue && !OnPathValue(this, Path, PL))
				{
					*Out = -1;
					return nullptr;
				}

				do
				{
					++Buffer;
				} while (*Buffer == ' ');
				if ((Buffer = ProcessVersion(Buffer, BufferEnd, Out)) == nullptr)
					return nullptr;

				if (*Buffer == '\015')
				{
					++Buffer;
					if (Buffer == BufferEnd)
					{
						*Out = -2;
						return nullptr;
					}

					if (*Buffer++ != '\012')
					{
						*Out = -1;
						return nullptr;
					}
				}
				else if (*Buffer != '\012')
				{
					*Out = -1;
					return nullptr;
				}
				else
					++Buffer;

				return ProcessHeaders(Buffer, BufferEnd, Out);
			}
			const char* Parser::ProcessResponse(const char* Buffer, const char* BufferEnd, int* Out)
			{
				TH_ASSERT(Buffer != nullptr, nullptr, "buffer should be set");
				TH_ASSERT(BufferEnd != nullptr, nullptr, "buffer end should be set");
				TH_ASSERT(Out != nullptr, nullptr, "output should be set");

				if ((Buffer = ProcessVersion(Buffer, BufferEnd, Out)) == nullptr)
					return nullptr;

				if (*Buffer != ' ')
				{
					*Out = -1;
					return nullptr;
				}

				do
				{
					++Buffer;
				} while (*Buffer == ' ');
				if (BufferEnd - Buffer < 4)
				{
					*Out = -2;
					return nullptr;
				}

				int Result = 0, Status = 0;
				if (*Buffer < '0' || '9' < *Buffer)
				{
					*Out = -1;
					return nullptr;
				}

				*(&Result) = 100 * (*Buffer++ - '0');
				Status = Result;
				if (*Buffer < '0' || '9' < *Buffer)
				{
					*Out = -1;
					return nullptr;
				}

				*(&Result) = 10 * (*Buffer++ - '0');
				Status += Result;
				if (*Buffer < '0' || '9' < *Buffer)
				{
					*Out = -1;
					return nullptr;
				}

				*(&Result) = (*Buffer++ - '0');
				Status += Result;
				if (OnStatusCode && !OnStatusCode(this, (int64_t)Status))
				{
					*Out = -1;
					return nullptr;
				}

				const char* Message; size_t MessageLength;
				if ((Buffer = Tokenize(Buffer, BufferEnd, &Message, &MessageLength, Out)) == nullptr)
					return nullptr;

				if (MessageLength == 0)
					return ProcessHeaders(Buffer, BufferEnd, Out);

				if (*Message != ' ')
				{
					*Out = -1;
					return nullptr;
				}

				do
				{
					++Message;
					--MessageLength;
				} while (*Message == ' ');
				if (OnStatusMessage && !OnStatusMessage(this, Message, MessageLength))
				{
					*Out = -1;
					return nullptr;
				}

				return ProcessHeaders(Buffer, BufferEnd, Out);
			}

			WebCodec::WebCodec() : State(Bytecode::Begin), Fragment(0)
			{
			}
			bool WebCodec::ParseFrame(const char* Buffer, size_t Size)
			{
				if (!Buffer || !Size)
					return !Queue.empty();

				if (Payload.capacity() <= Size)
					Payload.resize(Size);

				memcpy(Payload.data(), Buffer, sizeof(char) * Size);
				char* Data = Payload.data();

				while (Size)
				{
					uint8_t Index = *Data;
					switch (State)
					{
						case Bytecode::Begin:
						{
							uint8_t Op = Index & 0x0f;
							if (Index & 0x70)
								return !Queue.empty();

							Final = (Index & 0x80) ? 1 : 0;
							if (Op == 0)
							{
								if (!Fragment)
									return !Queue.empty();

								Control = 0;
							}
							else if (Op & 0x8)
							{
								if (Op != (uint8_t)WebSocketOp::Ping && Op != (uint8_t)WebSocketOp::Pong && Op != (uint8_t)WebSocketOp::Close)
									return !Queue.empty();

								if (!Final)
									return !Queue.empty();

								Control = 1;
								Opcode = (WebSocketOp)Op;
							}
							else
							{
								if (Op != (uint8_t)WebSocketOp::Text && Op != (uint8_t)WebSocketOp::Binary)
									return !Queue.empty();

								Control = 0;
								Fragment = !Final;
								Opcode = (WebSocketOp)Op;
							}

							State = Bytecode::Length;
							Data++; Size--;
							break;
						}
						case Bytecode::Length:
						{
							uint8_t Length = Index & 0x7f;
							Masked = (Index & 0x80) ? 1 : 0;
							Masks = 0;

							if (Control)
							{
								if (Length > 125)
									return !Queue.empty();

								Remains = Length;
								State = Masked ? Bytecode::Mask_0 : Bytecode::End;
							}
							else if (Length < 126)
							{
								Remains = Length;
								State = Masked ? Bytecode::Mask_0 : Bytecode::End;
							}
							else if (Length == 126)
								State = Bytecode::Length_16_0;
							else
								State = Bytecode::Length_64_0;

							Data++; Size--;
							if (State == Bytecode::End && Remains == 0)
							{
								Queue.emplace(std::make_pair(Opcode, std::vector<char>()));
								goto FetchPayload;
							}
							break;
						}
						case Bytecode::Length_16_0:
						{
							Remains = (uint64_t)Index << 8;
							State = Bytecode::Length_16_1;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_16_1:
						{
							Remains |= (uint64_t)Index << 0;
							State = Masked ? Bytecode::Mask_0 : Bytecode::End;
							if (Remains < 126)
								return !Queue.empty();

							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_0:
						{
							Remains = (uint64_t)Index << 56;
							State = Bytecode::Length_64_1;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_1:
						{
							Remains |= (uint64_t)Index << 48;
							State = Bytecode::Length_64_2;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_2:
						{
							Remains |= (uint64_t)Index << 40;
							State = Bytecode::Length_64_3;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_3:
						{
							Remains |= (uint64_t)Index << 32;
							State = Bytecode::Length_64_4;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_4:
						{
							Remains |= (uint64_t)Index << 24;
							State = Bytecode::Length_64_5;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_5:
						{
							Remains |= (uint64_t)Index << 16;
							State = Bytecode::Length_64_6;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_6:
						{
							Remains |= (uint64_t)Index << 8;
							State = Bytecode::Length_64_7;
							Data++; Size--;
							break;
						}
						case Bytecode::Length_64_7:
						{
							Remains |= (uint64_t)Index << 0;
							State = Masked ? Bytecode::Mask_0 : Bytecode::End;
							if (Remains < 65536)
								return !Queue.empty();

							Data++; Size--;
							break;
						}
						case Bytecode::Mask_0:
						{
							Mask[0] = Index;
							State = Bytecode::Mask_1;
							Data++; Size--;
							break;
						}
						case Bytecode::Mask_1:
						{
							Mask[1] = Index;
							State = Bytecode::Mask_2;
							Data++; Size--;
							break;
						}
						case Bytecode::Mask_2:
						{
							Mask[2] = Index;
							State = Bytecode::Mask_3;
							Data++; Size--;
							break;
						}
						case Bytecode::Mask_3:
						{
							Mask[3] = Index;
							State = Bytecode::End;
							Data++; Size--;
							if (Remains == 0)
							{
								Queue.emplace(std::make_pair(Opcode, std::vector<char>()));
								goto FetchPayload;
							}
							break;
						}
						case Bytecode::End:
						{
							size_t Length = Size;
							if (Length > Remains)
								Length = Remains;

							if (Masked)
							{
								for (size_t i = 0; i < Length; i++)
									Data[i] ^= Mask[Masks++ % 4];
							}

							std::vector<char> Message;
							TextAssign(Message, Data, Length);
							Queue.emplace(std::make_pair(Opcode, std::move(Message)));
							Opcode = WebSocketOp::Continue;

							Data += Length;
							Size -= Length;
							Remains -= Length;
							if (Remains == 0)
								goto FetchPayload;
							break;
						}
					}
				}

				return !Queue.empty();
			FetchPayload:
				if (!Control && !Final)
					return !Queue.empty();

				State = Bytecode::Begin;
				return true;
			}
			bool WebCodec::GetFrame(WebSocketOp* Op, std::vector<char>* Message)
			{
				TH_ASSERT(Op != nullptr, false, "op should be set");
				TH_ASSERT(Message != nullptr, false, "message should be set");

				if (Queue.empty())
					return false;

				auto& Base = Queue.front();
				*Message = std::move(Base.second);
				*Op = Base.first;
				Queue.pop();

				return true;
			}

			void Util::ConstructPath(Connection* Base)
			{
				TH_ASSERT_V(Base != nullptr && Base->Route != nullptr, "connection should be set");
				if (!Base->Route->Override.empty())
				{
					Base->Request.Path.assign(Base->Route->Override);
					if (Base->Route->Site->Callbacks.OnRewriteURL)
						Base->Route->Site->Callbacks.OnRewriteURL(Base);
					return;
				}

				for (uint64_t i = 0; i < Base->Request.URI.size(); i++)
				{
					if (Base->Request.URI[i] == '%' && i + 1 < Base->Request.URI.size())
					{
						if (Base->Request.URI[i + 1] == 'u')
						{
							int Value = 0;
							if (Compute::Common::HexToDecimal(Base->Request.URI, i + 2, 4, Value))
							{
								char Buffer[4];
								uint64_t LCount = Compute::Common::Utf8(Value, Buffer);
								if (LCount > 0)
									Base->Request.Path.append(Buffer, LCount);

								i += 5;
							}
							else
								Base->Request.Path += Base->Request.URI[i];
						}
						else
						{
							int Value = 0;
							if (Compute::Common::HexToDecimal(Base->Request.URI, i + 1, 2, Value))
							{
								Base->Request.Path += Value;
								i += 2;
							}
							else
								Base->Request.Path += Base->Request.URI[i];
						}
					}
					else if (Base->Request.URI[i] == '+')
						Base->Request.Path += ' ';
					else
						Base->Request.Path += Base->Request.URI[i];
				}

				char* Buffer = (char*)Base->Request.Path.c_str();
				char* Next = Buffer;
				while (Buffer[0] == '.' && Buffer[1] == '.')
					Buffer++;

				while (*Buffer != '\0')
				{
					*Next++ = *Buffer++;
					if (Buffer[-1] != '/' && Buffer[-1] != '\\')
						continue;

					while (Buffer[0] != '\0')
					{
						if (Buffer[0] == '/' || Buffer[0] == '\\')
							Buffer++;
						else if (Buffer[0] == '.' && Buffer[1] == '.')
							Buffer += 2;
						else
							break;
					}
				}

				*Next = '\0';
				if (!Base->Request.Match.Empty())
				{
					auto& Match = Base->Request.Match.Get()[0];
					Base->Request.Path = Base->Route->DocumentRoot + Core::Parser(Base->Request.Path).RemovePart(Match.Start, Match.End).R();
				}
				else
					Base->Request.Path = Base->Route->DocumentRoot + Base->Request.Path;

				Base->Request.Path = Core::OS::Path::Resolve(Base->Request.Path.c_str());
				if (Core::Parser(&Base->Request.Path).EndsOf("/\\"))
				{
					if (!Core::Parser(&Base->Request.URI).EndsOf("/\\"))
						Base->Request.Path.erase(Base->Request.Path.size() - 1, 1);
				}
				else if (Core::Parser(&Base->Request.URI).EndsOf("/\\"))
					Base->Request.Path.append(1, '/');

				if (Base->Route->Site->Callbacks.OnRewriteURL)
					Base->Route->Site->Callbacks.OnRewriteURL(Base);
			}
			void Util::ConstructHeadFull(RequestFrame* Request, ResponseFrame* Response, bool IsRequest, Core::Parser* Buffer)
			{
				TH_ASSERT_V(Request != nullptr, "connection should be set");
				TH_ASSERT_V(Response != nullptr, "response should be set");
				TH_ASSERT_V(Buffer != nullptr, "buffer should be set");

				HeaderMapping& Headers = (IsRequest ? Request->Headers : Response->Headers);
				for (auto& Item : Headers)
				{
					for (auto& Payload : Item.second)
						Buffer->fAppend("%s: %s\r\n", Item.first.c_str(), Payload.c_str());
				}

				if (IsRequest)
					return;

				for (auto& Item : Response->Cookies)
				{
					if (!Item.Domain.empty())
						Item.Domain.insert(0, "; domain=");

					if (!Item.Path.empty())
						Item.Path.insert(0, "; path=");

					Buffer->fAppend("Set-Cookie: %s=%s; expires=%s%s%s%s%s\r\n", Item.Name.c_str(), Item.Value.c_str(), Core::DateTime::GetGMTBasedString(Item.Expires).c_str(), Item.Path.c_str(), Item.Domain.c_str(), Item.Secure ? "; secure" : "", Item.HTTPOnly ? "; HTTPOnly" : "");
				}
			}
			void Util::ConstructHeadCache(Connection* Base, Core::Parser* Buffer)
			{
				TH_ASSERT_V(Base != nullptr && Base->Route != nullptr, "connection should be set");
				TH_ASSERT_V(Buffer != nullptr, "buffer should be set");

				if (!Base->Route->StaticFileMaxAge)
					return ConstructHeadUncache(Base, Buffer);

				Buffer->fAppend("Cache-Control: max-age=%llu\r\n", Base->Route->StaticFileMaxAge);
			}
			void Util::ConstructHeadUncache(Connection* Base, Core::Parser* Buffer)
			{
				TH_ASSERT_V(Base != nullptr, "connection should be set");
				TH_ASSERT_V(Buffer != nullptr, "buffer should be set");

				Buffer->Append(
					"Cache-Control: no-cache, no-store, must-revalidate, private, max-age=0\r\n"
					"Pragma: no-cache\r\n"
					"Expires: 0\r\n", 102);
			}
			bool Util::ConstructRoute(MapRouter* Router, Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				TH_ASSERT(Router != nullptr, false, "router should be set");

				if (Router->Sites.empty())
					return false;

				auto* Host = Base->Request.GetHeaderBlob("Host");
				if (!Host)
					return false;

				auto Listen = Router->Listeners.find(Core::Parser(Host).ToLower().R());
				if (Listen == Router->Listeners.end())
				{
					Listen = Router->Listeners.find("*");
					if (Listen == Router->Listeners.end())
						return false;
				}

				auto It = Router->Sites.find(Listen->first);
				if (It == Router->Sites.end())
					return false;

				Base->Request.Where = Base->Request.URI;
				for (auto& Group : It->second->Groups)
				{
					if (!Group.Match.empty())
					{
						Core::Parser URI(&Base->Request.URI);
						if (Group.Mode == RouteMode::Start)
						{
							if (!URI.StartsWith(Group.Match))
								continue;
							URI.Substring((uint64_t)Group.Match.size(), URI.Size());
						}
						else if (Group.Mode == RouteMode::Match)
						{
							if (!URI.Find(Group.Match).Found)
								continue;
						}
						else if (Group.Mode == RouteMode::End)
						{
							if (!URI.EndsWith(Group.Match))
								continue;
							URI.Clip(URI.Size() - (uint64_t)Group.Match.size());
						}

						if (URI.Empty())
							URI.Append('/');

						for (auto* Basis : Group.Routes)
						{
							if (Compute::Regex::Match(&Basis->URI, Base->Request.Match, Base->Request.URI))
							{
								Base->Route = Basis;
								return true;
							}
						}

						URI.Assign(Base->Request.Where);
					}
					else
					{
						for (auto* Basis : Group.Routes)
						{
							if (Compute::Regex::Match(&Basis->URI, Base->Request.Match, Base->Request.URI))
							{
								Base->Route = Basis;
								return true;
							}
						}
					}
				}

				Base->Route = It->second->Base;
				return true;
			}
			bool Util::ConstructDirectoryEntries(const Core::ResourceEntry& A, const Core::ResourceEntry& B)
			{
				if (A.Source.IsDirectory && !B.Source.IsDirectory)
					return true;

				if (!A.Source.IsDirectory && B.Source.IsDirectory)
					return false;

				auto Base = (HTTP::Connection*)A.UserData;
				if (!Base)
					return false;

				const char* Query = (Base->Request.Query.empty() ? nullptr : Base->Request.Query.c_str());
				if (Query != nullptr)
				{
					int Result = 0;
					if (*Query == 'n')
						Result = strcmp(A.Path.c_str(), B.Path.c_str());
					else if (*Query == 's')
						Result = (A.Source.Size == B.Source.Size) ? 0 : ((A.Source.Size > B.Source.Size) ? 1 : -1);
					else if (*Query == 'd')
						Result = (A.Source.LastModified == B.Source.LastModified) ? 0 : ((A.Source.LastModified > B.Source.LastModified) ? 1 : -1);

					if (Query[1] == 'a')
						return Result < 0;
					else if (Query[1] == 'd')
						return Result > 0;

					return Result < 0;
				}

				return strcmp(A.Path.c_str(), B.Path.c_str()) < 0;
			}
			bool Util::ContentOK(Content State)
			{
				return State == Content::Cached || State == Content::Empty || State == Content::Saved;
			}
			std::string Util::ConnectionResolve(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Root != nullptr && Base->Root->Router != nullptr, "Connection: Close\r\n", "connection should be set");
				if (Base->Info.KeepAlive <= 0)
					return "Connection: Close\r\n";

				if (Base->Response.StatusCode == 401)
					return "Connection: Close\r\n";

				if (Base->Root->Router->KeepAliveMaxCount == 0)
					return "Connection: Close\r\n";

				const char* Connection = Base->Request.GetHeader("Connection");
				if (Connection != nullptr && Core::Parser::CaseCompare(Connection, "keep-alive"))
				{
					Base->Info.KeepAlive = 0;
					return "Connection: Close\r\n";
				}

				if (!Connection && strcmp(Base->Request.Version, "1.1") != 0)
					return "Connection: Close\r\n";

				if (Base->Root->Router->KeepAliveMaxCount < 0)
					return "Connection: Keep-Alive\r\nKeep-Alive: timeout=" + std::to_string(Base->Root->Router->SocketTimeout / 1000) + "\r\n";

				return "Connection: Keep-Alive\r\nKeep-Alive: timeout=" + std::to_string(Base->Root->Router->SocketTimeout / 1000) + ", max=" + std::to_string(Base->Root->Router->KeepAliveMaxCount) + "\r\n";
			}
			std::string Util::ConstructContentRange(uint64_t Offset, uint64_t Length, uint64_t ContentLength)
			{
				std::string Field = "bytes ";
				Field += std::to_string(Offset);
				Field += '-';
				Field += std::to_string(Offset + Length - 1);
				Field += '/';
				Field += std::to_string(ContentLength);

				return Field;
			}
			const char* Util::ContentType(const std::string& Path, std::vector<MimeType>* Types)
			{
				TH_ASSERT(Types != nullptr, nullptr, "types should be set");
				static MimeStatic MimeTypes[] = { MimeStatic(".3dm", "x-world/x-3dmf"), MimeStatic(".3dmf", "x-world/x-3dmf"), MimeStatic(".a", "application/octet-stream"), MimeStatic(".aab", "application/x-authorware-bin"), MimeStatic(".aac", "audio/aac"), MimeStatic(".aam", "application/x-authorware-map"), MimeStatic(".aas", "application/x-authorware-seg"), MimeStatic(".aat", "application/font-sfnt"), MimeStatic(".abc", "text/vnd.abc"), MimeStatic(".acgi", "text/html"), MimeStatic(".afl", "video/animaflex"), MimeStatic(".ai", "application/postscript"), MimeStatic(".aif", "audio/x-aiff"), MimeStatic(".aifc", "audio/x-aiff"), MimeStatic(".aiff", "audio/x-aiff"), MimeStatic(".aim", "application/x-aim"), MimeStatic(".aip", "text/x-audiosoft-intra"), MimeStatic(".ani", "application/x-navi-animation"), MimeStatic(".aos", "application/x-nokia-9000-communicator-add-on-software"), MimeStatic(".aps", "application/mime"), MimeStatic(".arc", "application/octet-stream"), MimeStatic(".arj", "application/arj"), MimeStatic(".art", "image/x-jg"), MimeStatic(".asf", "video/x-ms-asf"), MimeStatic(".asm", "text/x-asm"), MimeStatic(".asp", "text/asp"), MimeStatic(".asx", "video/x-ms-asf"), MimeStatic(".au", "audio/x-au"), MimeStatic(".avi", "video/x-msvideo"), MimeStatic(".avs", "video/avs-video"), MimeStatic(".bcpio", "application/x-bcpio"), MimeStatic(".bin", "application/x-binary"), MimeStatic(".bm", "image/bmp"), MimeStatic(".bmp", "image/bmp"), MimeStatic(".boo", "application/book"), MimeStatic(".book", "application/book"), MimeStatic(".boz", "application/x-bzip2"), MimeStatic(".bsh", "application/x-bsh"), MimeStatic(".bz", "application/x-bzip"), MimeStatic(".bz2", "application/x-bzip2"), MimeStatic(".c", "text/x-c"), MimeStatic(".c++", "text/x-c"), MimeStatic(".cat", "application/vnd.ms-pki.seccat"), MimeStatic(".cc", "text/x-c"), MimeStatic(".ccad", "application/clariscad"), MimeStatic(".cco", "application/x-cocoa"), MimeStatic(".cdf", "application/x-cdf"), MimeStatic(".cer", "application/pkix-cert"), MimeStatic(".cff", "application/font-sfnt"), MimeStatic(".cha", "application/x-chat"), MimeStatic(".chat", "application/x-chat"), MimeStatic(".class", "application/x-java-class"), MimeStatic(".com", "application/octet-stream"), MimeStatic(".conf", "text/plain"), MimeStatic(".cpio", "application/x-cpio"), MimeStatic(".cpp", "text/x-c"), MimeStatic(".cpt", "application/x-compactpro"), MimeStatic(".crl", "application/pkcs-crl"), MimeStatic(".crt", "application/x-x509-user-cert"), MimeStatic(".csh", "text/x-script.csh"), MimeStatic(".css", "text/css"), MimeStatic(".csv", "text/csv"), MimeStatic(".cxx", "text/plain"), MimeStatic(".dcr", "application/x-director"), MimeStatic(".deepv", "application/x-deepv"), MimeStatic(".def", "text/plain"), MimeStatic(".der", "application/x-x509-ca-cert"), MimeStatic(".dif", "video/x-dv"), MimeStatic(".dir", "application/x-director"), MimeStatic(".dl", "video/x-dl"), MimeStatic(".dll", "application/octet-stream"), MimeStatic(".doc", "application/msword"), MimeStatic(".dot", "application/msword"), MimeStatic(".dp", "application/commonground"), MimeStatic(".drw", "application/drafting"), MimeStatic(".dump", "application/octet-stream"), MimeStatic(".dv", "video/x-dv"), MimeStatic(".dvi", "application/x-dvi"), MimeStatic(".dwf", "model/vnd.dwf"), MimeStatic(".dwg", "image/vnd.dwg"), MimeStatic(".dxf", "image/vnd.dwg"), MimeStatic(".dxr", "application/x-director"), MimeStatic(".el", "text/x-script.elisp"), MimeStatic(".elc", "application/x-bytecode.elisp"), MimeStatic(".env", "application/x-envoy"), MimeStatic(".eps", "application/postscript"), MimeStatic(".es", "application/x-esrehber"), MimeStatic(".etx", "text/x-setext"), MimeStatic(".evy", "application/x-envoy"), MimeStatic(".exe", "application/octet-stream"), MimeStatic(".f", "text/x-fortran"), MimeStatic(".f77", "text/x-fortran"), MimeStatic(".f90", "text/x-fortran"), MimeStatic(".fdf", "application/vnd.fdf"), MimeStatic(".fif", "image/fif"), MimeStatic(".fli", "video/x-fli"), MimeStatic(".flo", "image/florian"), MimeStatic(".flx", "text/vnd.fmi.flexstor"), MimeStatic(".fmf", "video/x-atomic3d-feature"), MimeStatic(".for", "text/x-fortran"), MimeStatic(".fpx", "image/vnd.fpx"), MimeStatic(".frl", "application/freeloader"), MimeStatic(".funk", "audio/make"), MimeStatic(".g", "text/plain"), MimeStatic(".g3", "image/g3fax"), MimeStatic(".gif", "image/gif"), MimeStatic(".gl", "video/x-gl"), MimeStatic(".gsd", "audio/x-gsm"), MimeStatic(".gsm", "audio/x-gsm"), MimeStatic(".gsp", "application/x-gsp"), MimeStatic(".gss", "application/x-gss"), MimeStatic(".gtar", "application/x-gtar"), MimeStatic(".gz", "application/x-gzip"), MimeStatic(".h", "text/x-h"), MimeStatic(".hdf", "application/x-hdf"), MimeStatic(".help", "application/x-helpfile"), MimeStatic(".hgl", "application/vnd.hp-hpgl"), MimeStatic(".hh", "text/x-h"), MimeStatic(".hlb", "text/x-script"), MimeStatic(".hlp", "application/x-helpfile"), MimeStatic(".hpg", "application/vnd.hp-hpgl"), MimeStatic(".hpgl", "application/vnd.hp-hpgl"), MimeStatic(".hqx", "application/binhex"), MimeStatic(".hta", "application/hta"), MimeStatic(".htc", "text/x-component"), MimeStatic(".htm", "text/html"), MimeStatic(".html", "text/html"), MimeStatic(".htmls", "text/html"), MimeStatic(".htt", "text/webviewhtml"), MimeStatic(".htx", "text/html"), MimeStatic(".ice", "x-conference/x-cooltalk"), MimeStatic(".ico", "image/x-icon"), MimeStatic(".idc", "text/plain"), MimeStatic(".ief", "image/ief"), MimeStatic(".iefs", "image/ief"), MimeStatic(".iges", "model/iges"), MimeStatic(".igs", "model/iges"), MimeStatic(".ima", "application/x-ima"), MimeStatic(".imap", "application/x-httpd-imap"), MimeStatic(".inf", "application/inf"), MimeStatic(".ins", "application/x-internett-signup"), MimeStatic(".ip", "application/x-ip2"), MimeStatic(".isu", "video/x-isvideo"), MimeStatic(".it", "audio/it"), MimeStatic(".iv", "application/x-inventor"), MimeStatic(".ivr", "i-world/i-vrml"), MimeStatic(".ivy", "application/x-livescreen"), MimeStatic(".jam", "audio/x-jam"), MimeStatic(".jav", "text/x-java-source"), MimeStatic(".java", "text/x-java-source"), MimeStatic(".jcm", "application/x-java-commerce"), MimeStatic(".jfif", "image/jpeg"), MimeStatic(".jfif-tbnl", "image/jpeg"), MimeStatic(".jpe", "image/jpeg"), MimeStatic(".jpeg", "image/jpeg"), MimeStatic(".jpg", "image/jpeg"), MimeStatic(".jpm", "image/jpm"), MimeStatic(".jps", "image/x-jps"), MimeStatic(".jpx", "image/jpx"), MimeStatic(".js", "application/x-javascript"), MimeStatic(".json", "application/json"), MimeStatic(".jut", "image/jutvision"), MimeStatic(".kar", "music/x-karaoke"), MimeStatic(".kml", "application/vnd.google-earth.kml+xml"), MimeStatic(".kmz", "application/vnd.google-earth.kmz"), MimeStatic(".ksh", "text/x-script.ksh"), MimeStatic(".la", "audio/x-nspaudio"), MimeStatic(".lam", "audio/x-liveaudio"), MimeStatic(".latex", "application/x-latex"), MimeStatic(".lha", "application/x-lha"), MimeStatic(".lhx", "application/octet-stream"), MimeStatic(".lib", "application/octet-stream"), MimeStatic(".list", "text/plain"), MimeStatic(".lma", "audio/x-nspaudio"), MimeStatic(".log", "text/plain"), MimeStatic(".lsp", "text/x-script.lisp"), MimeStatic(".lst", "text/plain"), MimeStatic(".lsx", "text/x-la-asf"), MimeStatic(".ltx", "application/x-latex"), MimeStatic(".lzh", "application/x-lzh"), MimeStatic(".lzx", "application/x-lzx"), MimeStatic(".m", "text/x-m"), MimeStatic(".m1v", "video/mpeg"), MimeStatic(".m2a", "audio/mpeg"), MimeStatic(".m2v", "video/mpeg"), MimeStatic(".m3u", "audio/x-mpegurl"), MimeStatic(".m4v", "video/x-m4v"), MimeStatic(".man", "application/x-troff-man"), MimeStatic(".map", "application/x-navimap"), MimeStatic(".mar", "text/plain"), MimeStatic(".mbd", "application/mbedlet"), MimeStatic(".mc$", "application/x-magic-cap-package-1.0"), MimeStatic(".mcd", "application/x-mathcad"), MimeStatic(".mcf", "text/mcf"), MimeStatic(".mcp", "application/netmc"), MimeStatic(".me", "application/x-troff-me"), MimeStatic(".mht", "message/rfc822"), MimeStatic(".mhtml", "message/rfc822"), MimeStatic(".mid", "audio/x-midi"), MimeStatic(".midi", "audio/x-midi"), MimeStatic(".mif", "application/x-mif"), MimeStatic(".mime", "www/mime"), MimeStatic(".mjf", "audio/x-vnd.audioexplosion.mjuicemediafile"), MimeStatic(".mjpg", "video/x-motion-jpeg"), MimeStatic(".mm", "application/base64"), MimeStatic(".mme", "application/base64"), MimeStatic(".mod", "audio/x-mod"), MimeStatic(".moov", "video/quicktime"), MimeStatic(".mov", "video/quicktime"), MimeStatic(".movie", "video/x-sgi-movie"), MimeStatic(".mp2", "video/x-mpeg"), MimeStatic(".mp3", "audio/x-mpeg-3"), MimeStatic(".mp4", "video/mp4"), MimeStatic(".mpa", "audio/mpeg"), MimeStatic(".mpc", "application/x-project"), MimeStatic(".mpeg", "video/mpeg"), MimeStatic(".mpg", "video/mpeg"), MimeStatic(".mpga", "audio/mpeg"), MimeStatic(".mpp", "application/vnd.ms-project"), MimeStatic(".mpt", "application/x-project"), MimeStatic(".mpv", "application/x-project"), MimeStatic(".mpx", "application/x-project"), MimeStatic(".mrc", "application/marc"), MimeStatic(".ms", "application/x-troff-ms"), MimeStatic(".mv", "video/x-sgi-movie"), MimeStatic(".my", "audio/make"), MimeStatic(".mzz", "application/x-vnd.audioexplosion.mzz"), MimeStatic(".nap", "image/naplps"), MimeStatic(".naplps", "image/naplps"), MimeStatic(".nc", "application/x-netcdf"), MimeStatic(".ncm", "application/vnd.nokia.configuration-message"), MimeStatic(".nif", "image/x-niff"), MimeStatic(".niff", "image/x-niff"), MimeStatic(".nix", "application/x-mix-transfer"), MimeStatic(".nsc", "application/x-conference"), MimeStatic(".nvd", "application/x-navidoc"), MimeStatic(".o", "application/octet-stream"), MimeStatic(".obj", "application/octet-stream"), MimeStatic(".oda", "application/oda"), MimeStatic(".oga", "audio/ogg"), MimeStatic(".ogg", "audio/ogg"), MimeStatic(".ogv", "video/ogg"), MimeStatic(".omc", "application/x-omc"), MimeStatic(".omcd", "application/x-omcdatamaker"), MimeStatic(".omcr", "application/x-omcregerator"), MimeStatic(".otf", "application/font-sfnt"), MimeStatic(".p", "text/x-pascal"), MimeStatic(".p10", "application/x-pkcs10"), MimeStatic(".p12", "application/x-pkcs12"), MimeStatic(".p7a", "application/x-pkcs7-signature"), MimeStatic(".p7c", "application/x-pkcs7-mime"), MimeStatic(".p7m", "application/x-pkcs7-mime"), MimeStatic(".p7r", "application/x-pkcs7-certreqresp"), MimeStatic(".p7s", "application/pkcs7-signature"), MimeStatic(".part", "application/pro_eng"), MimeStatic(".pas", "text/x-pascal"), MimeStatic(".pbm", "image/x-portable-bitmap"), MimeStatic(".pcl", "application/vnd.hp-pcl"), MimeStatic(".pct", "image/x-pct"), MimeStatic(".pcx", "image/x-pcx"), MimeStatic(".pdb", "chemical/x-pdb"), MimeStatic(".pdf", "application/pdf"), MimeStatic(".pfr", "application/font-tdpfr"), MimeStatic(".pfunk", "audio/make"), MimeStatic(".pgm", "image/x-portable-greymap"), MimeStatic(".pic", "image/pict"), MimeStatic(".pict", "image/pict"), MimeStatic(".pkg", "application/x-newton-compatible-pkg"), MimeStatic(".pko", "application/vnd.ms-pki.pko"), MimeStatic(".pl", "text/x-script.perl"), MimeStatic(".plx", "application/x-pixelscript"), MimeStatic(".pm", "text/x-script.perl-module"), MimeStatic(".pm4", "application/x-pagemaker"), MimeStatic(".pm5", "application/x-pagemaker"), MimeStatic(".png", "image/png"), MimeStatic(".pnm", "image/x-portable-anymap"), MimeStatic(".pot", "application/vnd.ms-powerpoint"), MimeStatic(".pov", "model/x-pov"), MimeStatic(".ppa", "application/vnd.ms-powerpoint"), MimeStatic(".ppm", "image/x-portable-pixmap"), MimeStatic(".pps", "application/vnd.ms-powerpoint"), MimeStatic(".ppt", "application/vnd.ms-powerpoint"), MimeStatic(".ppz", "application/vnd.ms-powerpoint"), MimeStatic(".pre", "application/x-freelance"), MimeStatic(".prt", "application/pro_eng"), MimeStatic(".ps", "application/postscript"), MimeStatic(".psd", "application/octet-stream"), MimeStatic(".pvu", "paleovu/x-pv"), MimeStatic(".pwz", "application/vnd.ms-powerpoint"), MimeStatic(".py", "text/x-script.python"), MimeStatic(".pyc", "application/x-bytecode.python"), MimeStatic(".qcp", "audio/vnd.qcelp"), MimeStatic(".qd3", "x-world/x-3dmf"), MimeStatic(".qd3d", "x-world/x-3dmf"), MimeStatic(".qif", "image/x-quicktime"), MimeStatic(".qt", "video/quicktime"), MimeStatic(".qtc", "video/x-qtc"), MimeStatic(".qti", "image/x-quicktime"), MimeStatic(".qtif", "image/x-quicktime"), MimeStatic(".ra", "audio/x-pn-realaudio"), MimeStatic(".ram", "audio/x-pn-realaudio"), MimeStatic(".rar", "application/x-arj-compressed"), MimeStatic(".ras", "image/x-cmu-raster"), MimeStatic(".rast", "image/cmu-raster"), MimeStatic(".rexx", "text/x-script.rexx"), MimeStatic(".rf", "image/vnd.rn-realflash"), MimeStatic(".rgb", "image/x-rgb"), MimeStatic(".rm", "audio/x-pn-realaudio"), MimeStatic(".rmi", "audio/mid"), MimeStatic(".rmm", "audio/x-pn-realaudio"), MimeStatic(".rmp", "audio/x-pn-realaudio"), MimeStatic(".rng", "application/vnd.nokia.ringing-tone"), MimeStatic(".rnx", "application/vnd.rn-realplayer"), MimeStatic(".roff", "application/x-troff"), MimeStatic(".rp", "image/vnd.rn-realpix"), MimeStatic(".rpm", "audio/x-pn-realaudio-plugin"), MimeStatic(".rt", "text/vnd.rn-realtext"), MimeStatic(".rtf", "application/x-rtf"), MimeStatic(".rtx", "application/x-rtf"), MimeStatic(".rv", "video/vnd.rn-realvideo"), MimeStatic(".s", "text/x-asm"), MimeStatic(".s3m", "audio/s3m"), MimeStatic(".saveme", "application/octet-stream"), MimeStatic(".sbk", "application/x-tbook"), MimeStatic(".scm", "text/x-script.scheme"), MimeStatic(".sdml", "text/plain"), MimeStatic(".sdp", "application/x-sdp"), MimeStatic(".sdr", "application/sounder"), MimeStatic(".sea", "application/x-sea"), MimeStatic(".set", "application/set"), MimeStatic(".sgm", "text/x-sgml"), MimeStatic(".sgml", "text/x-sgml"), MimeStatic(".sh", "text/x-script.sh"), MimeStatic(".shar", "application/x-shar"), MimeStatic(".shtm", "text/html"), MimeStatic(".shtml", "text/html"), MimeStatic(".sid", "audio/x-psid"), MimeStatic(".sil", "application/font-sfnt"), MimeStatic(".sit", "application/x-sit"), MimeStatic(".skd", "application/x-koan"), MimeStatic(".skm", "application/x-koan"), MimeStatic(".skp", "application/x-koan"), MimeStatic(".skt", "application/x-koan"), MimeStatic(".sl", "application/x-seelogo"), MimeStatic(".smi", "application/smil"), MimeStatic(".smil", "application/smil"), MimeStatic(".snd", "audio/x-adpcm"), MimeStatic(".so", "application/octet-stream"), MimeStatic(".sol", "application/solids"), MimeStatic(".spc", "text/x-speech"), MimeStatic(".spl", "application/futuresplash"), MimeStatic(".spr", "application/x-sprite"), MimeStatic(".sprite", "application/x-sprite"), MimeStatic(".src", "application/x-wais-source"), MimeStatic(".ssi", "text/x-server-parsed-html"), MimeStatic(".ssm", "application/streamingmedia"), MimeStatic(".sst", "application/vnd.ms-pki.certstore"), MimeStatic(".step", "application/step"), MimeStatic(".stl", "application/vnd.ms-pki.stl"), MimeStatic(".stp", "application/step"), MimeStatic(".sv4cpio", "application/x-sv4cpio"), MimeStatic(".sv4crc", "application/x-sv4crc"), MimeStatic(".svf", "image/x-dwg"), MimeStatic(".svg", "image/svg+xml"), MimeStatic(".svr", "x-world/x-svr"), MimeStatic(".swf", "application/x-shockwave-flash"), MimeStatic(".t", "application/x-troff"), MimeStatic(".talk", "text/x-speech"), MimeStatic(".tar", "application/x-tar"), MimeStatic(".tbk", "application/x-tbook"), MimeStatic(".tcl", "text/x-script.tcl"), MimeStatic(".tcsh", "text/x-script.tcsh"), MimeStatic(".tex", "application/x-tex"), MimeStatic(".texi", "application/x-texinfo"), MimeStatic(".texinfo", "application/x-texinfo"), MimeStatic(".text", "text/plain"), MimeStatic(".tgz", "application/x-compressed"), MimeStatic(".tif", "image/x-tiff"), MimeStatic(".tiff", "image/x-tiff"), MimeStatic(".torrent", "application/x-bittorrent"), MimeStatic(".tr", "application/x-troff"), MimeStatic(".tsi", "audio/tsp-audio"), MimeStatic(".tsp", "audio/tsplayer"), MimeStatic(".tsv", "text/tab-separated-values"), MimeStatic(".ttf", "application/font-sfnt"), MimeStatic(".turbot", "image/florian"), MimeStatic(".txt", "text/plain"), MimeStatic(".uil", "text/x-uil"), MimeStatic(".uni", "text/uri-list"), MimeStatic(".unis", "text/uri-list"), MimeStatic(".unv", "application/i-deas"), MimeStatic(".uri", "text/uri-list"), MimeStatic(".uris", "text/uri-list"), MimeStatic(".ustar", "application/x-ustar"), MimeStatic(".uu", "text/x-uuencode"), MimeStatic(".uue", "text/x-uuencode"), MimeStatic(".vcd", "application/x-cdlink"), MimeStatic(".vcs", "text/x-vcalendar"), MimeStatic(".vda", "application/vda"), MimeStatic(".vdo", "video/vdo"), MimeStatic(".vew", "application/groupwise"), MimeStatic(".viv", "video/vnd.vivo"), MimeStatic(".vivo", "video/vnd.vivo"), MimeStatic(".vmd", "application/vocaltec-media-desc"), MimeStatic(".vmf", "application/vocaltec-media-resource"), MimeStatic(".voc", "audio/x-voc"), MimeStatic(".vos", "video/vosaic"), MimeStatic(".vox", "audio/voxware"), MimeStatic(".vqe", "audio/x-twinvq-plugin"), MimeStatic(".vqf", "audio/x-twinvq"), MimeStatic(".vql", "audio/x-twinvq-plugin"), MimeStatic(".vrml", "model/vrml"), MimeStatic(".vrt", "x-world/x-vrt"), MimeStatic(".vsd", "application/x-visio"), MimeStatic(".vst", "application/x-visio"), MimeStatic(".vsw", "application/x-visio"), MimeStatic(".w60", "application/wordperfect6.0"), MimeStatic(".w61", "application/wordperfect6.1"), MimeStatic(".w6w", "application/msword"), MimeStatic(".wav", "audio/x-wav"), MimeStatic(".wb1", "application/x-qpro"), MimeStatic(".wbmp", "image/vnd.wap.wbmp"), MimeStatic(".web", "application/vnd.xara"), MimeStatic(".webm", "video/webm"), MimeStatic(".wiz", "application/msword"), MimeStatic(".wk1", "application/x-123"), MimeStatic(".wmf", "windows/metafile"), MimeStatic(".wml", "text/vnd.wap.wml"), MimeStatic(".wmlc", "application/vnd.wap.wmlc"), MimeStatic(".wmls", "text/vnd.wap.wmlscript"), MimeStatic(".wmlsc", "application/vnd.wap.wmlscriptc"), MimeStatic(".woff", "application/font-woff"), MimeStatic(".word", "application/msword"), MimeStatic(".wp", "application/wordperfect"), MimeStatic(".wp5", "application/wordperfect"), MimeStatic(".wp6", "application/wordperfect"), MimeStatic(".wpd", "application/wordperfect"), MimeStatic(".wq1", "application/x-lotus"), MimeStatic(".wri", "application/x-wri"), MimeStatic(".wrl", "model/vrml"), MimeStatic(".wrz", "model/vrml"), MimeStatic(".wsc", "text/scriplet"), MimeStatic(".wsrc", "application/x-wais-source"), MimeStatic(".wtk", "application/x-wintalk"), MimeStatic(".x-png", "image/png"), MimeStatic(".xbm", "image/x-xbm"), MimeStatic(".xdr", "video/x-amt-demorun"), MimeStatic(".xgz", "xgl/drawing"), MimeStatic(".xhtml", "application/xhtml+xml"), MimeStatic(".xif", "image/vnd.xiff"), MimeStatic(".xl", "application/vnd.ms-excel"), MimeStatic(".xla", "application/vnd.ms-excel"), MimeStatic(".xlb", "application/vnd.ms-excel"), MimeStatic(".xlc", "application/vnd.ms-excel"), MimeStatic(".xld", "application/vnd.ms-excel"), MimeStatic(".xlk", "application/vnd.ms-excel"), MimeStatic(".xll", "application/vnd.ms-excel"), MimeStatic(".xlm", "application/vnd.ms-excel"), MimeStatic(".xls", "application/vnd.ms-excel"), MimeStatic(".xlt", "application/vnd.ms-excel"), MimeStatic(".xlv", "application/vnd.ms-excel"), MimeStatic(".xlw", "application/vnd.ms-excel"), MimeStatic(".xm", "audio/xm"), MimeStatic(".xml", "text/xml"), MimeStatic(".xmz", "xgl/movie"), MimeStatic(".xpix", "application/x-vnd.ls-xpix"), MimeStatic(".xpm", "image/x-xpixmap"), MimeStatic(".xsl", "application/xml"), MimeStatic(".xslt", "application/xml"), MimeStatic(".xsr", "video/x-amt-showrun"), MimeStatic(".xwd", "image/x-xwd"), MimeStatic(".xyz", "chemical/x-pdb"), MimeStatic(".z", "application/x-compressed"), MimeStatic(".zip", "application/x-zip-compressed"), MimeStatic(".zoo", "application/octet-stream"), MimeStatic(".zsh", "text/x-script.zsh") };

				uint64_t PathLength = Path.size();
				while (PathLength >= 1 && Path[PathLength - 1] != '.')
					PathLength--;

				if (!PathLength)
					return "application/octet-stream";

				const char* Ext = &Path.c_str()[PathLength - 1];
				int End = ((int)(sizeof(MimeTypes) / sizeof(MimeTypes[0])));
				int Start = 0, Result, Index;

				while (End - Start > 1)
				{
					Index = (Start + End) >> 1;
					if ((Result = Core::Parser::CaseCompare(Ext, MimeTypes[Index].Extension)) == 0)
						return MimeTypes[Index].Type;

					if (Result < 0)
						End = Index;
					else
						Start = Index;
				}

				if (!Core::Parser::CaseCompare(Ext, MimeTypes[Start].Extension))
					return MimeTypes[Start].Type;

				if (!Types->empty())
				{
					for (auto& Item : *Types)
					{
						if (!Core::Parser::CaseCompare(Ext, Item.Extension.c_str()))
							return Item.Type.c_str();
					}
				}

				return "application/octet-stream";
			}
			const char* Util::StatusMessage(int StatusCode)
			{
				switch (StatusCode)
				{
					case 100:
						return "Continue";
					case 101:
						return "Switching Protocols";
					case 102:
						return "Processing";
					case 200:
						return "OK";
					case 201:
						return "Created";
					case 202:
						return "Accepted";
					case 203:
						return "Non-Authoritative Information";
					case 204:
						return "No Content";
					case 205:
						return "Reset Content";
					case 206:
						return "Partial Content";
					case 207:
						return "Multi-Status";
					case 208:
						return "Already Reported";
					case 226:
						return "IM used";
					case 300:
						return "Multiple Choices";
					case 301:
						return "Moved Permanently";
					case 302:
						return "Found";
					case 303:
						return "See Other";
					case 304:
						return "Not Modified";
					case 305:
						return "Use Proxy";
					case 307:
						return "Temporary Redirect";
					case 308:
						return "Permanent Redirect";
					case 400:
						return "Bad Request";
					case 401:
						return "Unauthorized";
					case 402:
						return "Payment Required";
					case 403:
						return "Forbidden";
					case 404:
						return "Not Found";
					case 405:
						return "Method Not Allowed";
					case 406:
						return "Not Acceptable";
					case 407:
						return "Proxy Authentication Required";
					case 408:
						return "Request Time-out";
					case 409:
						return "Conflict";
					case 410:
						return "Gone";
					case 411:
						return "Length Required";
					case 412:
						return "Precondition Failed";
					case 413:
						return "Request Entity Too Large";
					case 414:
						return "Request URI Too Large";
					case 415:
						return "Unsupported Media Type";
					case 416:
						return "Requested range not satisfiable";
					case 417:
						return "Expectation Failed";
					case 418:
						return "I am a teapot";
					case 419:
						return "Authentication Timeout";
					case 420:
						return "Enhance Your Calm";
					case 421:
						return "Misdirected Request";
					case 422:
						return "Unproccessable entity";
					case 423:
						return "Locked";
					case 424:
						return "Failed Dependency";
					case 426:
						return "Upgrade Required";
					case 428:
						return "Precondition Required";
					case 429:
						return "Too Many Requests";
					case 431:
						return "Request Header Fields Too Large";
					case 440:
						return "Login Timeout";
					case 451:
						return "Unavailable For Legal Reasons";
					case 500:
						return "Internal Server Error";
					case 501:
						return "Not Implemented";
					case 502:
						return "Bad Gateway";
					case 503:
						return "Service Unavailable";
					case 504:
						return "Gateway Time-out";
					case 505:
						return "Version not supported";
					case 506:
						return "Variant Also Negotiates";
					case 507:
						return "Insufficient Storage";
					case 508:
						return "Loop Detected";
					case 509:
						return "Bandwidth Limit Exceeded";
					case 510:
						return "Not Extended";
					case 511:
						return "Network Authentication Required";
					default:
						if (StatusCode >= 100 && StatusCode < 200)
							return "Information";

						if (StatusCode >= 200 && StatusCode < 300)
							return "Success";

						if (StatusCode >= 300 && StatusCode < 400)
							return "Redirection";

						if (StatusCode >= 400 && StatusCode < 500)
							return "Client Error";

						if (StatusCode >= 500 && StatusCode < 600)
							return "Server Error";
						break;
				}

				return "Stateless";
			}
			bool Util::ParseMultipartHeaderField(Parser* Parser, const char* Name, size_t Length)
			{
				return ParseHeaderField(Parser, Name, Length);
			}
			bool Util::ParseMultipartHeaderValue(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				if (!Length)
					return true;

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Segment)
					return true;

				if (!Segment->Header.empty())
				{
					std::string Value(Data, Length);
					if (Segment->Header == "Content-Disposition")
					{
						Core::Parser::Settle Start = Core::Parser(&Value).Find("name=\"");
						if (Start.Found)
						{
							Core::Parser::Settle End = Core::Parser(&Value).Find('\"', Start.End);
							if (End.Found)
								Segment->Source.Key = Value.substr(Start.End, End.End - Start.End - 1);
						}

						Start = Core::Parser(&Value).Find("filename=\"");
						if (Start.Found)
						{
							Core::Parser::Settle End = Core::Parser(&Value).Find('\"', Start.End);
							if (End.Found)
								Segment->Source.Name = Value.substr(Start.End, End.End - Start.End - 1);
						}
					}
					else if (Segment->Header == "Content-Type")
						Segment->Source.Type = Value;

					Segment->Source.SetHeader(Segment->Header.c_str(), Value);
					Segment->Header.clear();
				}

				return true;
			}
			bool Util::ParseMultipartContentData(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				if (!Length)
					return true;

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Segment || !Segment->Stream)
					return false;

				if (fwrite(Data, 1, (size_t)Length, Segment->Stream) != (size_t)Length)
					return false;

				Segment->Source.Length += Length;
				return true;
			}
			bool Util::ParseMultipartResourceBegin(Parser* Parser)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Segment || !Segment->Request)
					return true;

				if (Segment->Stream != nullptr)
				{
					TH_CLOSE(Segment->Stream);
					return false;
				}

				if (Segment->Route && Segment->Request->Resources.size() >= Segment->Route->Site->MaxResources)
				{
					Segment->Close = true;
					return false;
				}

				Segment->Header.clear();
				Segment->Source.Headers.clear();
				Segment->Source.Name.clear();
				Segment->Source.Type = "application/octet-stream";
				Segment->Source.Memory = false;
				Segment->Source.Length = 0;

				if (Segment->Route)
					Segment->Source.Path = Segment->Route->Site->ResourceRoot + Compute::Common::MD5Hash(Compute::Common::RandomBytes(16));

				Segment->Stream = (FILE*)Core::OS::File::Open(Segment->Source.Path.c_str(), "wb");
				return Segment->Stream != nullptr;
			}
			bool Util::ParseMultipartResourceEnd(Parser* Parser)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Segment || !Segment->Stream || !Segment->Request)
					return true;

				TH_CLOSE(Segment->Stream);
				Segment->Stream = nullptr;
				Segment->Request->Resources.push_back(Segment->Source);

				if (Segment->Callback)
					Segment->Callback(nullptr, &Segment->Request->Resources.back());

				return true;
			}
			bool Util::ParseHeaderField(Parser* Parser, const char* Name, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Name != nullptr, true, "name should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment)
					return true;

				Segment->Header.assign(Name, Length);
				return true;
			}
			bool Util::ParseHeaderValue(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment || Segment->Header.empty())
					return true;

				if (Core::Parser::CaseCompare(Segment->Header.c_str(), "cookie") == 0)
				{
					std::vector<std::pair<std::string, std::string>> Cookies;
					const char* Offset = Data;

					for (uint64_t i = 0; i < Length; i++)
					{
						if (Data[i] != '=')
							continue;

						std::string Name(Offset, (size_t)((Data + i) - Offset));
						size_t Set = i;

						while (i + 1 < Length && Data[i] != ';')
							i++;

						if (Data[i] == ';')
							i--;

						Cookies.emplace_back(std::make_pair(std::move(Name), std::string(Data + Set + 1, i - Set)));
						Offset = Data + (i + 3);
					}

					if (Segment->Request)
					{
						for (auto&& Item : Cookies)
						{
							auto& Cookie = Segment->Request->Cookies[Item.first];
							Cookie.emplace_back(std::move(Item.second));
						}
					}
				}
				else
				{
					if (Segment->Request)
						Segment->Request->Headers[Segment->Header].emplace_back(Data, Length);

					if (Segment->Response)
						Segment->Response->Headers[Segment->Header].emplace_back(Data, Length);
				}

				Segment->Header.clear();
				return true;
			}
			bool Util::ParseVersion(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment || !Segment->Request)
					return true;

				memcpy((void*)Segment->Request->Version, (void*)Data, std::min<size_t>(Length, sizeof(Segment->Request->Version)));
				return true;
			}
			bool Util::ParseStatusCode(Parser* Parser, size_t Value)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Segment || !Segment->Response)
					return true;

				Segment->Response->StatusCode = (int)Value;
				return true;
			}
			bool Util::ParseMethodValue(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment || !Segment->Request)
					return true;

				memcpy((void*)Segment->Request->Method, (void*)Data, std::min<size_t>(Length, sizeof(Segment->Request->Method)));
				return true;
			}
			bool Util::ParsePathValue(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment || !Segment->Request)
					return true;

				Segment->Request->URI.assign(Data, Length);
				return true;
			}
			bool Util::ParseQueryValue(Parser* Parser, const char* Data, size_t Length)
			{
				TH_ASSERT(Parser != nullptr, true, "parser should be set");
				TH_ASSERT(Data != nullptr, true, "data should be set");

				ParserFrame* Segment = (ParserFrame*)Parser->UserPointer;
				if (!Length || !Segment || !Segment->Request)
					return true;

				Segment->Request->Query.assign(Data, Length);
				return true;
			}
			int Util::ParseContentRange(const char* ContentRange, int64_t* Range1, int64_t* Range2)
			{
				TH_ASSERT(ContentRange != nullptr, 0, "content range should be set");
				TH_ASSERT(Range1 != nullptr, 0, "range 1 should be set");
				TH_ASSERT(Range2 != nullptr, 0, "range 2 should be set");

				return sscanf(ContentRange, "bytes=%lld-%lld", Range1, Range2);
			}
			std::string Util::ParseMultipartDataBoundary()
			{
				static const char Data[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

				std::random_device SeedGenerator;
				std::mt19937 Engine(SeedGenerator());
				std::string Result = "--sha1-digest-multipart-data-";

				for (int i = 0; i < 16; i++)
					Result += Data[Engine() % (sizeof(Data) - 1)];

				return Result;
			}
			bool Util::Authorize(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				if (Base->Route->Auth.Type.empty())
					return true;

				bool IsSupported = false;
				for (auto& Item : Base->Route->Auth.Methods)
				{
					if (Item == Base->Request.Method)
					{
						IsSupported = true;
						break;
					}
				}

				if (!IsSupported && !Base->Route->Auth.Methods.empty())
				{
					Base->Request.User.Type = Auth::Denied;
					Base->Error(401, "Authorization method is not allowed");
					return false;
				}

				const char* Authorization = Base->Request.GetHeader("Authorization");
				if (!Authorization)
				{
					Base->Request.User.Type = Auth::Denied;
					Base->Error(401, "Provide authorization header to continue.");
					return false;
				}

				uint64_t Index = 0;
				while (Authorization[Index] != ' ' && Authorization[Index] != '\0')
					Index++;

				std::string Type = std::string(Authorization, Index);
				std::string Credentials = Compute::Common::Base64Decode(Authorization + Index + 1);
				Index = 0;

				while (Credentials[Index] != ':' && Credentials[Index] != '\0')
					Index++;

				Base->Request.User.Username = std::string(Credentials.c_str(), Index);
				Base->Request.User.Password = std::string(Credentials.c_str() + Index + 1);
				if (Base->Route->Callbacks.Authorize && Base->Route->Callbacks.Authorize(Base, &Base->Request.User, Type))
				{
					Base->Request.User.Type = Auth::Granted;
					return true;
				}

				if (Type != Base->Route->Auth.Type)
				{
					Base->Request.User.Type = Auth::Denied;
					Base->Error(401, "Authorization type \"%s\" is not allowed.", Type.c_str());
					return false;
				}

				for (auto& Item : Base->Route->Auth.Users)
				{
					if (Item.Password != Base->Request.User.Password || Item.Username != Base->Request.User.Username)
						continue;

					Base->Request.User.Type = Auth::Granted;
					return true;
				}

				Base->Request.User.Type = Auth::Denied;
				Base->Error(401, "Invalid user access credentials were provided. Access denied.");
				return false;
			}
			bool Util::MethodAllowed(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				for (auto& Item : Base->Route->DisallowedMethods)
				{
					if (Item == Base->Request.Method)
						return false;
				}

				return true;
			}
			bool Util::WebSocketUpgradeAllowed(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				const char* Upgrade = Base->Request.GetHeader("Upgrade");
				if (!Upgrade)
					return false;

				if (Core::Parser::CaseCompare(Upgrade, "websocket") != 0)
					return false;

				const char* Connection = Base->Request.GetHeader("Connection");
				if (!Connection)
					return false;

				if (Core::Parser::CaseCompare(Connection, "upgrade") != 0)
					return false;

				return true;
			}
			bool Util::ResourceHidden(Connection* Base, std::string* Path)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				if (Base->Route->HiddenFiles.empty())
					return false;

				const std::string& Value = (Path ? *Path : Base->Request.Path);
				Compute::RegexResult Result;

				for (auto& Item : Base->Route->HiddenFiles)
				{
					if (Compute::Regex::Match(&Item, Result, Value))
						return true;
				}

				return false;
			}
			bool Util::ResourceIndexed(Connection* Base, Core::Resource* Resource)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(Resource != nullptr, false, "resource should be set");

				if (Base->Route->IndexFiles.empty())
					return false;

				std::string Path = Base->Request.Path;
				if (!Core::Parser(&Path).EndsOf("/\\"))
				{
#ifdef TH_MICROSOFT
					Path.append(1, '\\');
#else
					Path.append(1, '/');
#endif
				}

				for (auto& Item : Base->Route->IndexFiles)
				{
					if (!Core::OS::File::State(Path + Item, Resource))
						continue;

					Base->Request.Path.assign(Path.append(Item));
					return true;
				}

				return false;
			}
			bool Util::ResourceProvided(Connection* Base, Core::Resource* Resource)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(Resource != nullptr, false, "resource should be set");

				if (!Base->Route->Site->Gateway.Enabled)
					return false;

				if (!Base->Route->Gateway.Methods.empty())
				{
					for (auto& Item : Base->Route->Gateway.Methods)
					{
						if (Item == Base->Request.Method)
							return false;
					}
				}

				if (Base->Route->Gateway.Files.empty())
					return false;

				Compute::RegexResult Result;
				for (auto& Item : Base->Route->Gateway.Files)
				{
					if (!Compute::Regex::Match(&Item, Result, Base->Request.Path))
						continue;

					return Resource->Size > 0;
				}

				return false;
			}
			bool Util::ResourceModified(Connection* Base, Core::Resource* Resource)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(Resource != nullptr, false, "resource should be set");

				const char* CacheControl = Base->Request.GetHeader("Cache-Control");
				if (CacheControl != nullptr && (!Core::Parser::CaseCompare("no-cache", CacheControl) || !Core::Parser::CaseCompare("max-age=0", CacheControl)))
					return true;

				const char* IfNoneMatch = Base->Request.GetHeader("If-None-Match");
				if (IfNoneMatch != nullptr)
				{
					char ETag[64];
					Core::OS::Net::GetETag(ETag, sizeof(ETag), Resource);
					if (!Core::Parser::CaseCompare(ETag, IfNoneMatch))
						return false;
				}

				const char* IfModifiedSince = Base->Request.GetHeader("If-Modified-Since");
				return !(IfModifiedSince != nullptr && Resource->LastModified <= Core::DateTime::ReadGMTBasedString(IfModifiedSince));

			}
			bool Util::ResourceCompressed(Connection* Base, uint64_t Size)
			{
#ifdef TH_HAS_ZLIB
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				if (!Base->Route->Compression.Enabled || Size < Base->Route->Compression.MinLength)
					return false;

				if (Base->Route->Compression.Files.empty())
					return true;

				Compute::RegexResult Result;
				for (auto& Item : Base->Route->Compression.Files)
				{
					if (Compute::Regex::Match(&Item, Result, Base->Request.Path))
						return true;
				}

				return false;
#else
				return false;
#endif
			}
			bool Util::RouteWEBSOCKET(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				if (!Base->Route || !Base->Route->AllowWebSocket)
					return Base->Error(404, "Web Socket protocol is not allowed on this server.");

				const char* WebSocketKey = Base->Request.GetHeader("Sec-WebSocket-Key");
				if (WebSocketKey != nullptr)
					return ProcessWebSocket(Base, WebSocketKey);

				const char* WebSocketKey1 = Base->Request.GetHeader("Sec-WebSocket-Key1");
				if (!WebSocketKey1)
					return Base->Error(400, "Malformed websocket request. Provide first key.");

				const char* WebSocketKey2 = Base->Request.GetHeader("Sec-WebSocket-Key2");
				if (!WebSocketKey2)
					return Base->Error(400, "Malformed websocket request. Provide second key.");

				return Base->Stream->ReadAsync(8, [Base](NetEvent Event, const char* Buffer, size_t Recv)
				{
					if (Packet::IsData(Event))
						Base->Request.Buffer.append(Buffer, Recv);
					else if (Packet::IsDone(Event))
						Util::ProcessWebSocket(Base, Base->Request.Buffer.c_str());
					else if (Packet::IsError(Event))
						Base->Break();

					return true;
				});
			}
			bool Util::RouteGET(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				if (!Core::OS::File::State(Base->Request.Path, &Base->Resource))
				{
					if (WebSocketUpgradeAllowed(Base))
					{
						return Core::Schedule::Get()->SetTask([Base]()
						{
							RouteWEBSOCKET(Base);
						});
					}

					return Base->Error(404, "Requested resource was not found.");
				}

				if (WebSocketUpgradeAllowed(Base))
				{
					return Core::Schedule::Get()->SetTask([Base]()
					{
						RouteWEBSOCKET(Base);
					});
				}

				if (ResourceHidden(Base, nullptr))
					return Base->Error(404, "Requested resource was not found.");

				if (Base->Resource.IsDirectory && !ResourceIndexed(Base, &Base->Resource))
				{
					if (Base->Route->AllowDirectoryListing)
					{
						return Core::Schedule::Get()->SetTask([Base]()
						{
							ProcessDirectory(Base);
						});
					}

					return Base->Error(403, "Directory listing denied.");
				}

				if (ResourceProvided(Base, &Base->Resource))
					return ProcessGateway(Base);

				if (Base->Route->StaticFileMaxAge > 0 && !ResourceModified(Base, &Base->Resource))
				{
					return Core::Schedule::Get()->SetTask([Base]()
					{
						ProcessResourceCache(Base);
					});
				}

				if (Base->Resource.Size > Base->Root->Router->PayloadMaxLength)
					return Base->Error(413, "Entity payload is too big to process.");

				return Core::Schedule::Get()->SetTask([Base]()
				{
					ProcessResource(Base);
				});
			}
			bool Util::RoutePOST(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				if (!Base->Route)
					return Base->Error(404, "Requested resource was not found.");

				if (!Core::OS::File::State(Base->Request.Path, &Base->Resource))
					return Base->Error(404, "Requested resource was not found.");

				if (ResourceHidden(Base, nullptr))
					return Base->Error(404, "Requested resource was not found.");

				if (Base->Resource.IsDirectory && !ResourceIndexed(Base, &Base->Resource))
					return Base->Error(404, "Requested resource was not found.");

				if (ResourceProvided(Base, &Base->Resource))
					return ProcessGateway(Base);

				if (Base->Route->StaticFileMaxAge > 0 && !ResourceModified(Base, &Base->Resource))
				{
					return Core::Schedule::Get()->SetTask([Base]()
					{
						ProcessResourceCache(Base);
					});
				}

				if (Base->Resource.Size > Base->Root->Router->PayloadMaxLength)
					return Base->Error(413, "Entity payload is too big to process.");

				return Core::Schedule::Get()->SetTask([Base]()
				{
					ProcessResource(Base);
				});
			}
			bool Util::RoutePUT(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				if (!Base->Route || ResourceHidden(Base, nullptr))
					return Base->Error(403, "Resource overwrite denied.");

				if (!Core::OS::File::State(Base->Request.Path, &Base->Resource))
					return Base->Error(403, "Directory overwrite denied.");

				if (ResourceProvided(Base, &Base->Resource))
					return ProcessGateway(Base);

				if (!Base->Resource.IsDirectory)
					return Base->Error(403, "Directory overwrite denied.");

				const char* Range = Base->Request.GetHeader("Range");
				int64_t Range1 = 0, Range2 = 0;

				FILE* Stream = (FILE*)Core::OS::File::Open(Base->Request.Path.c_str(), "wb");
				if (!Stream)
					return Base->Error(422, "Resource stream cannot be opened.");

				if (Range != nullptr && HTTP::Util::ParseContentRange(Range, &Range1, &Range2))
				{
					if (Base->Response.StatusCode <= 0)
						Base->Response.StatusCode = 206;
#ifdef TH_MICROSOFT
					if (_lseeki64(TH_FILENO(Stream), Range1, SEEK_SET) != 0)
						return Base->Error(416, "Invalid content range offset (%lld) was specified.", Range1);
#elif defined(TH_APPLE)
					if (fseek(Stream, Range1, SEEK_SET) != 0)
						return Base->Error(416, "Invalid content range offset (%lld) was specified.", Range1);
#else
					if (lseek64(TH_FILENO(Stream), Range1, SEEK_SET) != 0)
						return Base->Error(416, "Invalid content range offset (%lld) was specified.", Range1);
#endif
				}
				else
					Base->Response.StatusCode = 204;

				return Base->Consume([=](Connection* Base, NetEvent Event, const char* Buffer, size_t Size)
				{
					if (Packet::IsData(Event))
					{
						fwrite(Buffer, sizeof(char) * (size_t)Size, 1, Stream);
						return true;
					}
					else if (Packet::IsDone(Event))
					{
						char Date[64];
						Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

						Core::Parser Content;
						Content.fAppend("%s 204 No Content\r\nDate: %s\r\n%sContent-Location: %s\r\n", Base->Request.Version, Date, Util::ConnectionResolve(Base).c_str(), Base->Request.URI.c_str());

						TH_CLOSE(Stream);
						if (Base->Route->Callbacks.Headers)
							Base->Route->Callbacks.Headers(Base, nullptr);

						Content.Append("\r\n", 2);
						return !Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
						{
							if (Packet::IsDone(Event))
								Base->Finish();
							else if (Packet::IsError(Event))
								Base->Break();
						});
					}
					else if (Packet::IsError(Event))
					{
						TH_CLOSE(Stream);
						return Base->Break();
					}
					else if (Packet::IsSkip(Event))
						TH_CLOSE(Stream);

					return true;
				});
			}
			bool Util::RoutePATCH(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				if (!Base->Route)
					return Base->Error(403, "Operation denied by server.");

				if (!Core::OS::File::State(Base->Request.Path, &Base->Resource))
					return Base->Error(404, "Requested resource was not found.");

				if (ResourceHidden(Base, nullptr))
					return Base->Error(404, "Requested resource was not found.");

				if (Base->Resource.IsDirectory && !ResourceIndexed(Base, &Base->Resource))
					return Base->Error(404, "Requested resource cannot be directory.");

				if (ResourceProvided(Base, &Base->Resource))
					return ProcessGateway(Base);

				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				Core::Parser Content;
				Content.fAppend("%s 204 No Content\r\nDate: %s\r\n%sContent-Location: %s\r\n", Base->Request.Version, Date, Util::ConnectionResolve(Base).c_str(), Base->Request.URI.c_str());

				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, nullptr);

				Content.Append("\r\n", 2);
				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
						Base->Finish(204);
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::RouteDELETE(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				if (!Base->Route || ResourceHidden(Base, nullptr))
					return Base->Error(403, "Operation denied by server.");

				if (!Core::OS::File::State(Base->Request.Path, &Base->Resource))
					return Base->Error(404, "Requested resource was not found.");

				if (ResourceProvided(Base, &Base->Resource))
					return ProcessGateway(Base);

				if (!Base->Resource.IsDirectory)
				{
					if (Core::OS::File::Remove(Base->Request.Path.c_str()) != 0)
						return Base->Error(403, "Operation denied by system.");
				}
				else if (Core::OS::Directory::Remove(Base->Request.Path.c_str()) != 0)
					return Base->Error(403, "Operation denied by system.");

				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				Core::Parser Content;
				Content.fAppend("%s 204 No Content\r\nDate: %s\r\n%s", Base->Request.Version, Date, Util::ConnectionResolve(Base).c_str());

				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, nullptr);

				Content.Append("\r\n", 2);
				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
						Base->Finish(204);
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::RouteOPTIONS(Connection* Base)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				Core::Parser Content;
				Content.fAppend("%s 204 No Content\r\nDate: %s\r\n%sAllow: GET, POST, PUT, PATCH, DELETE, OPTIONS, HEAD\r\n", Base->Request.Version, Date, Util::ConnectionResolve(Base).c_str());

				if (Base->Route && Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				Content.Append("\r\n", 2);
				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
						Base->Finish(204);
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::ProcessDirectory(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				std::vector<Core::ResourceEntry> Entries;
				if (!Core::OS::Directory::Scan(Base->Request.Path, &Entries))
					return Base->Error(500, "System denied to directory listing.");

				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				Core::Parser Content;
				Content.fAppend("%s 200 OK\r\nDate: %s\r\n%sContent-Type: text/html; charset=%s\r\nAccept-Ranges: bytes\r\n", Base->Request.Version, Date, ConnectionResolve(Base).c_str(), Base->Route->CharSet.c_str());

				ConstructHeadCache(Base, &Content);
				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				const char* Message = Base->Response.GetHeader("X-Error");
				if (Message != nullptr)
					Content.fAppend("X-Error: %s\n\r", Message);

				uint64_t Size = Base->Request.URI.size() - 1;
				while (Base->Request.URI[Size] != '/')
					Size--;

				char Direction = (!Base->Request.Query.empty() && Base->Request.Query[1] == 'd') ? 'a' : 'd';
				std::string Name = Compute::Common::URIDecode(Base->Request.URI);
				std::string Parent(1, '/');
				if (Base->Request.URI.size() > 1)
				{
					Parent = Base->Request.URI.substr(0, Base->Request.URI.size() - 1);
					Parent = Core::OS::Path::GetDirectory(Parent.c_str());
				}

				TextAssign(Base->Response.Buffer,
					"<html><head><title>Index of " + Name + "</title>"
					"<style>th {text-align: left;}</style></head>"
					"<body><h1>Index of " + Name + "</h1><pre><table cellpadding=\"0\">"
					"<tr><th><a href=\"?n" + Direction + "\">Name</a></th>"
					"<th><a href=\"?d" + Direction + "\">Modified</a></th>"
					"<th><a href=\"?s" + Direction + "\">Size</a></th></tr>"
					"<tr><td colspan=\"3\"><hr></td></tr>"
					"<tr><td><a href=\"" + Parent + "\">Parent directory</a></td>"
					"<td>&nbsp;-</td><td>&nbsp;&nbsp;-</td></tr>");

				for (auto& Item : Entries)
					Item.UserData = Base;

				std::sort(Entries.begin(), Entries.end(), Util::ConstructDirectoryEntries);
				for (auto& Item : Entries)
				{
					if (ResourceHidden(Base, &Item.Path))
						continue;

					char dSize[64];
					if (!Item.Source.IsDirectory)
					{
						if (Item.Source.Size < 1024)
							snprintf(dSize, sizeof(dSize), "%db", (int)Item.Source.Size);
						else if (Item.Source.Size < 0x100000)
							snprintf(dSize, sizeof(dSize), "%.1fk", ((double)Item.Source.Size) / 1024.0);
						else if (Item.Source.Size < 0x40000000)
							snprintf(dSize, sizeof(Size), "%.1fM", ((double)Item.Source.Size) / 1048576.0);
						else
							snprintf(dSize, sizeof(dSize), "%.1fG", ((double)Item.Source.Size) / 1073741824.0);
					}
					else
						strcpy(dSize, "[DIRECTORY]");

					char dDate[64];
					Core::DateTime::TimeFormatLCL(dDate, sizeof(dDate), Item.Source.LastModified);

					std::string URI = Compute::Common::URIEncode(Item.Path);
					std::string HREF = (Base->Request.URI + ((*(Base->Request.URI.c_str() + 1) != '\0' && Base->Request.URI[Base->Request.URI.size() - 1] != '/') ? "/" : "") + URI);
					if (Item.Source.IsDirectory && !Core::Parser(&HREF).EndsOf("/\\"))
						HREF.append(1, '/');

					TextAppend(Base->Response.Buffer, "<tr><td><a href=\"" + HREF + "\">" + Item.Path + "</a></td><td>&nbsp;" + dDate + "</td><td>&nbsp;&nbsp;" + dSize + "</td></tr>\n");
				}
				TextAppend(Base->Response.Buffer, "</table></pre></body></html>");

#ifdef TH_HAS_ZLIB
				bool Deflate = false, Gzip = false;
				if (Util::ResourceCompressed(Base, Base->Response.Buffer.size()))
				{
					const char* AcceptEncoding = Base->Request.GetHeader("Accept-Encoding");
					if (AcceptEncoding != nullptr)
					{
						Deflate = strstr(AcceptEncoding, "deflate") != nullptr;
						Gzip = strstr(AcceptEncoding, "gzip") != nullptr;
					}

					if (AcceptEncoding != nullptr && (Deflate || Gzip))
					{
						z_stream Stream;
						Stream.zalloc = Z_NULL;
						Stream.zfree = Z_NULL;
						Stream.opaque = Z_NULL;
						Stream.avail_in = (uInt)Base->Response.Buffer.size();
						Stream.next_in = (Bytef*)Base->Response.Buffer.data();

						if (deflateInit2(&Stream, Base->Route->Compression.QualityLevel, Z_DEFLATED, (Gzip ? 15 | 16 : 15), Base->Route->Compression.MemoryLevel, (int)Base->Route->Compression.Tune) == Z_OK)
						{
							std::string Buffer(Base->Response.Buffer.size(), '\0');
							Stream.avail_out = (uInt)Buffer.size();
							Stream.next_out = (Bytef*)Buffer.c_str();
							bool Compress = (deflate(&Stream, Z_FINISH) == Z_STREAM_END);
							bool Flush = (deflateEnd(&Stream) == Z_OK);

							if (Compress && Flush)
							{
								TextAssign(Base->Response.Buffer, Buffer.c_str(), (uint64_t)Stream.total_out);
								if (!Base->Response.GetHeader("Content-Encoding"))
								{
									if (Gzip)
										Content.Append("Content-Encoding: gzip\r\n", 24);
									else
										Content.Append("Content-Encoding: deflate\r\n", 27);
								}
							}
						}
					}
				}
#endif
				Content.fAppend("Content-Length: %llu\r\n\r\n", (uint64_t)Base->Response.Buffer.size());
				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						if (memcmp(Base->Request.Method, "HEAD", 4) == 0)
							return (void)Base->Finish(200);

						Base->Stream->WriteAsync(Base->Response.Buffer.data(), (int64_t)Base->Response.Buffer.size(), [Base](NetEvent Event, size_t Sent)
						{
							if (Packet::IsDone(Event))
								Base->Finish(200);
							else if (Packet::IsError(Event))
								Base->Break();
						});
					}
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::ProcessResource(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				const char* ContentType = Util::ContentType(Base->Request.Path, &Base->Route->MimeTypes);
				const char* Range = Base->Request.GetHeader("Range");
				const char* StatusMessage = Util::StatusMessage(Base->Response.StatusCode = (Base->Response.Error && Base->Response.StatusCode > 0 ? Base->Response.StatusCode : 200));
				int64_t Range1 = 0, Range2 = 0, Count = 0;
				int64_t ContentLength = Base->Resource.Size;

				char ContentRange[128] = { 0 };
				if (Range != nullptr && (Count = Util::ParseContentRange(Range, &Range1, &Range2)) > 0 && Range1 >= 0 && Range2 >= 0)
				{
					if (Count == 2)
						ContentLength = ((Range2 > ContentLength) ? ContentLength : Range2) - Range1 + 1;
					else
						ContentLength -= Range1;

					snprintf(ContentRange, sizeof(ContentRange), "Content-Range: bytes %lld-%lld/%lld\r\n", Range1, Range1 + ContentLength - 1, (int64_t)Base->Resource.Size);
					StatusMessage = Util::StatusMessage(Base->Response.StatusCode = (Base->Response.Error ? Base->Response.StatusCode : 206));
				}
#ifdef TH_HAS_ZLIB
				if (Util::ResourceCompressed(Base, (uint64_t)ContentLength))
				{
					const char* AcceptEncoding = Base->Request.GetHeader("Accept-Encoding");
					if (AcceptEncoding != nullptr)
					{
						bool Deflate = strstr(AcceptEncoding, "deflate") != nullptr;
						bool Gzip = strstr(AcceptEncoding, "gzip") != nullptr;

						if (Deflate || Gzip)
							return ProcessResourceCompress(Base, Deflate, Gzip, ContentRange, Range1);
					}
				}
#endif
				const char* Origin = Base->Request.GetHeader("Origin");
				const char* CORS1 = "", * CORS2 = "", * CORS3 = "";
				if (Origin != nullptr)
				{
					CORS1 = "Access-Control-Allow-Origin: ";
					CORS2 = Base->Route->AccessControlAllowOrigin.c_str();
					CORS3 = "\r\n";
				}

				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				char LastModified[64];
				Core::DateTime::TimeFormatGMT(LastModified, sizeof(LastModified), Base->Resource.LastModified);

				char ETag[64];
				Core::OS::Net::GetETag(ETag, sizeof(ETag), &Base->Resource);

				Core::Parser Content;
				Content.fAppend("%s %d %s\r\n%s%s%sDate: %s\r\n", Base->Request.Version, Base->Response.StatusCode, StatusMessage, CORS1, CORS2, CORS3, Date);

				Util::ConstructHeadCache(Base, &Content);
				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				const char* Message = Base->Response.GetHeader("X-Error");
				if (Message != nullptr)
					Content.fAppend("X-Error: %s\n\r", Message);

				Content.fAppend("Accept-Ranges: bytes\r\n"
					"Last-Modified: %s\r\nEtag: %s\r\n"
					"Content-Type: %s; charset=%s\r\n"
					"Content-Length: %lld\r\n"
					"%s%s\r\n", LastModified, ETag, ContentType, Base->Route->CharSet.c_str(), ContentLength, Util::ConnectionResolve(Base).c_str(), ContentRange);

				if (!ContentLength || !strcmp(Base->Request.Method, "HEAD"))
				{
					return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
					{
						if (Packet::IsDone(Event))
							Base->Finish(200);
						else if (Packet::IsError(Event))
							Base->Break();
					});
				}

				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base, ContentLength, Range1](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						Core::Schedule::Get()->SetTask([Base, ContentLength, Range1]()
						{
							Util::ProcessFile(Base, ContentLength, Range1);
						});
					}
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::ProcessResourceCompress(Connection* Base, bool Deflate, bool Gzip, const char* ContentRange, uint64_t Range)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(ContentRange != nullptr, false, "content tange should be set");
				TH_ASSERT(Deflate || Gzip, false, "uncompressable resource");

				const char* ContentType = Util::ContentType(Base->Request.Path, &Base->Route->MimeTypes);
				const char* StatusMessage = Util::StatusMessage(Base->Response.StatusCode = (Base->Response.Error && Base->Response.StatusCode > 0 ? Base->Response.StatusCode : 200));
				int64_t ContentLength = Base->Resource.Size;

				const char* Origin = Base->Request.GetHeader("Origin");
				const char* CORS1 = "", * CORS2 = "", * CORS3 = "";
				if (Origin != nullptr)
				{
					CORS1 = "Access-Control-Allow-Origin: ";
					CORS2 = Base->Route->AccessControlAllowOrigin.c_str();
					CORS3 = "\r\n";
				}

				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				char LastModified[64];
				Core::DateTime::TimeFormatGMT(LastModified, sizeof(LastModified), Base->Resource.LastModified);

				char ETag[64];
				Core::OS::Net::GetETag(ETag, sizeof(ETag), &Base->Resource);

				Core::Parser Content;
				Content.fAppend("%s %d %s\r\n%s%s%sDate: %s\r\n", Base->Request.Version, Base->Response.StatusCode, StatusMessage, CORS1, CORS2, CORS3, Date);

				Util::ConstructHeadCache(Base, &Content);
				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				const char* Message = Base->Response.GetHeader("X-Error");
				if (Message != nullptr)
					Content.fAppend("X-Error: %s\n\r", Message);

				Content.fAppend("Accept-Ranges: bytes\r\n"
					"Last-Modified: %s\r\nEtag: %s\r\n"
					"Content-Type: %s; charset=%s\r\n"
					"Content-Encoding: %s\r\n"
					"Transfer-Encoding: chunked\r\n"
					"%s%s\r\n", LastModified, ETag, ContentType, Base->Route->CharSet.c_str(), (Gzip ? "gzip" : "deflate"), Util::ConnectionResolve(Base).c_str(), ContentRange);

				if (!ContentLength || !strcmp(Base->Request.Method, "HEAD"))
				{
					return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
					{
						if (Packet::IsDone(Event))
							Base->Finish();
						else if (Packet::IsError(Event))
							Base->Break();
					});
				}

				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base, Range, ContentLength, Gzip](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						Core::Schedule::Get()->SetTask([Base, Range, ContentLength, Gzip]()
						{
							Util::ProcessFileCompress(Base, ContentLength, Range, Gzip);
						});
					}
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::ProcessResourceCache(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				char Date[64];
				Core::DateTime::TimeFormatGMT(Date, sizeof(Date), Base->Info.Start / 1000);

				char LastModified[64];
				Core::DateTime::TimeFormatGMT(LastModified, sizeof(LastModified), Base->Resource.LastModified);

				char ETag[64];
				Core::OS::Net::GetETag(ETag, sizeof(ETag), &Base->Resource);

				Core::Parser Content;
				Content.fAppend("%s 304 %s\r\nDate: %s\r\n", Base->Request.Version, HTTP::Util::StatusMessage(304), Date);

				Util::ConstructHeadCache(Base, &Content);
				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				Content.fAppend("Accept-Ranges: bytes\r\nLast-Modified: %s\r\nEtag: %s\r\n%s\r\n", LastModified, ETag, Util::ConnectionResolve(Base).c_str());
				return Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
						Base->Finish(304);
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}
			bool Util::ProcessFile(Connection* Base, uint64_t ContentLength, uint64_t Range)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				Range = (Range > Base->Resource.Size ? Base->Resource.Size : Range);
				if (ContentLength > 0 && Base->Resource.IsReferenced && Base->Resource.Size > 0)
				{
					uint64_t sLimit = Base->Resource.Size - Range;
					if (ContentLength > sLimit)
						ContentLength = sLimit;

					if (Base->Response.Buffer.size() >= ContentLength)
					{
						return Base->Stream->WriteAsync(Base->Response.Buffer.data() + Range, (int64_t)ContentLength, [Base](NetEvent Event, size_t Sent)
						{
							if (Packet::IsDone(Event))
								Base->Finish();
							else if (Packet::IsError(Event))
								Base->Break();
						});
					}
				}

				FILE* Stream = (FILE*)Core::OS::File::Open(Base->Request.Path.c_str(), "rb");
				if (!Stream)
					return Base->Error(500, "System denied to open resource stream.");

#ifdef TH_MICROSOFT
				if (Range > 0 && _lseeki64(TH_FILENO(Stream), Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#elif defined(TH_APPLE)
				if (Range > 0 && fseek(Stream, Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#else
				if (Range > 0 && lseek64(TH_FILENO(Stream), Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#endif
				Server* Router = Base->Root;
				if (!Base->Route->AllowSendFile)
					return ProcessFileChunk(Base, Router, Stream, ContentLength);

				Base->Stream->SetBlocking(true);
				Base->Stream->SetTimeout((int)Base->Root->Router->SocketTimeout);
				bool Result = Core::OS::Net::SendFile(Stream, Base->Stream->GetFd(), ContentLength);
				Base->Stream->SetTimeout(0);
				Base->Stream->SetBlocking(false);

				if (Router->State != ServerState::Working)
				{
					TH_CLOSE(Stream);
					return Base->Break();
				}

				if (!Result)
					return ProcessFileChunk(Base, Router, Stream, ContentLength);

				TH_CLOSE(Stream);
				return Base->Finish();
			}
			bool Util::ProcessFileChunk(Connection* Base, Server* Router, FILE* Stream, uint64_t ContentLength)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(Router != nullptr, false, "router should be set");
				TH_ASSERT(Stream != nullptr, false, "stream should be set");

				if (!ContentLength || Router->State != ServerState::Working)
				{
				Cleanup:
					TH_CLOSE(Stream);
					if (Router->State != ServerState::Working)
						return Base->Break();

					return Base->Finish() || true;
				}

				char Buffer[8192]; int Read = sizeof(Buffer);
				if ((Read = (int)fread(Buffer, 1, (size_t)(Read > ContentLength ? ContentLength : Read), Stream)) <= 0)
					goto Cleanup;

				ContentLength -= (int64_t)Read;
				Base->Stream->WriteAsync(Buffer, Read, [Base, Router, Stream, ContentLength](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						Core::Schedule::Get()->SetTask([Base, Router, Stream, ContentLength]()
						{
							ProcessFileChunk(Base, Router, Stream, ContentLength);
						});
					}
					else if (Packet::IsError(Event))
					{
						TH_CLOSE(Stream);
						Base->Break();
					}
					else if (Packet::IsSkip(Event))
						TH_CLOSE(Stream);
				});

				return false;
			}
			bool Util::ProcessFileCompress(Connection* Base, uint64_t ContentLength, uint64_t Range, bool Gzip)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				Range = (Range > Base->Resource.Size ? Base->Resource.Size : Range);
				if (ContentLength > 0 && Base->Resource.IsReferenced && Base->Resource.Size > 0)
				{
					if (Base->Response.Buffer.size() >= ContentLength)
					{
#ifdef TH_HAS_ZLIB
						z_stream ZStream;
						ZStream.zalloc = Z_NULL;
						ZStream.zfree = Z_NULL;
						ZStream.opaque = Z_NULL;
						ZStream.avail_in = (uInt)Base->Response.Buffer.size();
						ZStream.next_in = (Bytef*)Base->Response.Buffer.data();

						if (deflateInit2(&ZStream, Base->Route->Compression.QualityLevel, Z_DEFLATED, (Gzip ? 15 | 16 : 15), Base->Route->Compression.MemoryLevel, (int)Base->Route->Compression.Tune) == Z_OK)
						{
							std::string Buffer(Base->Response.Buffer.size(), '\0');
							ZStream.avail_out = (uInt)Buffer.size();
							ZStream.next_out = (Bytef*)Buffer.c_str();
							bool Compress = (deflate(&ZStream, Z_FINISH) == Z_STREAM_END);
							bool Flush = (deflateEnd(&ZStream) == Z_OK);

							if (Compress && Flush)
								TextAssign(Base->Response.Buffer, Buffer.c_str(), (uint64_t)ZStream.total_out);
						}
#endif
						return Base->Stream->WriteAsync(Base->Response.Buffer.data(), (int64_t)ContentLength, [Base](NetEvent Event, size_t Sent)
						{
							if (Packet::IsDone(Event))
								Base->Finish();
							else if (Packet::IsError(Event))
								Base->Break();
						});
					}
				}

				FILE* Stream = (FILE*)Core::OS::File::Open(Base->Request.Path.c_str(), "rb");
				if (!Stream)
					return Base->Error(500, "System denied to open resource stream.");

#ifdef TH_MICROSOFT
				if (Range > 0 && _lseeki64(TH_FILENO(Stream), Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#elif defined(TH_APPLE)
				if (Range > 0 && fseek(Stream, Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#else
				if (Range > 0 && lseek64(TH_FILENO(Stream), Range, SEEK_SET) == -1)
				{
					TH_CLOSE(Stream);
					return Base->Error(400, "Provided content range offset (%llu) is invalid", Range);
				}
#endif
#ifdef TH_HAS_ZLIB
				Server* Server = Base->Root;
				z_stream* ZStream = (z_stream*)TH_MALLOC(sizeof(z_stream));
				ZStream->zalloc = Z_NULL;
				ZStream->zfree = Z_NULL;
				ZStream->opaque = Z_NULL;

				if (deflateInit2(ZStream, Base->Route->Compression.QualityLevel, Z_DEFLATED, (Gzip ? MAX_WBITS + 16 : MAX_WBITS), Base->Route->Compression.MemoryLevel, (int)Base->Route->Compression.Tune) != Z_OK)
				{
					TH_CLOSE(Stream);
					TH_FREE(ZStream);
					return Base->Break();
				}

				return ProcessFileCompressChunk(Base, Server, Stream, ZStream, ContentLength);
#else
				TH_CLOSE(Stream);
				return Base->Error(500, "Cannot process gzip stream.");
#endif
			}
			bool Util::ProcessFileCompressChunk(Connection* Base, Server* Router, FILE* Stream, void* CStream, uint64_t ContentLength)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				TH_ASSERT(Router != nullptr, false, "router should be set");
				TH_ASSERT(Stream != nullptr, false, "stream should be set");
				TH_ASSERT(CStream != nullptr, false, "cstream should be set");
#ifdef TH_HAS_ZLIB
#define FREE_STREAMING { TH_CLOSE(Stream); deflateEnd(ZStream); TH_FREE(ZStream); }
				z_stream* ZStream = (z_stream*)CStream;
				if (!ContentLength || Router->State != ServerState::Working)
				{
				Cleanup:
					FREE_STREAMING;
					if (Router->State != ServerState::Working)
						return Base->Break();

					return Base->Stream->WriteAsync("0\r\n\r\n", 5, [Base](NetEvent Event, size_t Sent)
					{
						if (Packet::IsDone(Event))
							Base->Finish();
						else if (Packet::IsError(Event))
							Base->Break();
					}) || true;
				}

				static const int Head = 17;
				char Buffer[8192 + Head], Deflate[8192];
				int Read = sizeof(Buffer) - Head;

				if ((Read = (int)fread(Buffer, 1, (size_t)(Read > ContentLength ? ContentLength : Read), Stream)) <= 0)
					goto Cleanup;

				ContentLength -= (int64_t)Read;
				ZStream->avail_in = (uInt)Read;
				ZStream->next_in = (Bytef*)Buffer;
				ZStream->avail_out = (uInt)sizeof(Deflate);
				ZStream->next_out = (Bytef*)Deflate;
				deflate(ZStream, Z_SYNC_FLUSH);
				Read = (int)sizeof(Deflate) - (int)ZStream->avail_out;

				int Next = snprintf(Buffer, sizeof(Buffer), "%X\r\n", Read);
				memcpy(Buffer + Next, Deflate, Read);
				Read += Next;

				if (!ContentLength)
				{
					memcpy(Buffer + Read, "\r\n0\r\n\r\n", sizeof(char) * 7);
					Read += sizeof(char) * 7;
				}
				else
				{
					memcpy(Buffer + Read, "\r\n", sizeof(char) * 2);
					Read += sizeof(char) * 2;
				}

				Base->Stream->WriteAsync(Buffer, Read, [Base, Router, Stream, ZStream, ContentLength](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						if (ContentLength > 0)
						{
							Core::Schedule::Get()->SetTask([Base, Router, Stream, ZStream, ContentLength]()
							{
								ProcessFileCompressChunk(Base, Router, Stream, ZStream, ContentLength);
							});
						}
						else
						{
							FREE_STREAMING;
							Base->Finish();
						}
					}
					else if (Packet::IsError(Event))
					{
						FREE_STREAMING;
						Base->Break();
					}
					else if (Packet::IsSkip(Event))
						FREE_STREAMING;
				});

				return false;
#undef FREE_STREAMING
#else
				return Base->Finish();
#endif
			}
			bool Util::ProcessGateway(Connection* Base)
			{
				TH_ASSERT(Base != nullptr && Base->Route != nullptr, false, "connection should be set");
				if (!Base->Route->Callbacks.Compiler)
					return Base->Error(400, "Gateway cannot be issued.") && false;

				Script::VMManager* VM = ((MapRouter*)Base->Root->GetRouter())->VM;
				if (!VM)
					return Base->Error(500, "Gateway cannot be issued.") && false;

				return Core::Schedule::Get()->SetTask([Base, VM]()
				{
					Script::VMCompiler* Compiler = VM->CreateCompiler();
					if (Compiler->Prepare(Core::OS::Path::GetFilename(Base->Request.Path.c_str()), Base->Request.Path, true, true) < 0)
					{
						TH_RELEASE(Compiler);
						return (void)Base->Error(500, "Gateway module cannot be prepared.");
					}

					char* Buffer = nullptr;
					if (Base->Route->Callbacks.Compiler)
					{
						if (!Base->Route->Callbacks.Compiler(Base, Compiler))
						{
							TH_RELEASE(Compiler);
							return (void)Base->Error(500, "Gateway creation exception.");
						}
					}

					size_t Size = 0;
					if (!Compiler->IsCached())
					{
						FILE* Stream = (FILE*)Core::OS::File::Open(Base->Request.Path.c_str(), "rb");
						if (!Stream)
							return (void)Base->Error(404, "Gateway resource was not found.");

						Size = Base->Resource.Size;
						Buffer = (char*)TH_MALLOC((size_t)(Size + 1) * sizeof(char));

						if (fread(Buffer, 1, (size_t)Size, Stream) != (size_t)Size)
						{
							TH_CLOSE(Stream);
							TH_FREE(Buffer);
							return (void)Base->Error(500, "Gateway resource stream exception.");
						}

						Buffer[Size] = '\0';
						TH_CLOSE(Stream);
					}

					Core::Schedule::Get()->SetTask([Base, Compiler, Buffer, Size]()
					{
						Base->Gateway = TH_NEW(GatewayFrame, Compiler);
						Base->Gateway->E.Exception = [Base](GatewayFrame* Gateway)
						{
							Base->Response.StatusCode = 500;
							if (Base->Route->Gateway.ReportErrors)
							{
								const char* Exception, *Function; int Line, Column;
								if (Gateway->GetException(&Exception, &Function, &Line, &Column))
									Base->Info.Message = Core::Form("%s() at line %i\n%s.", Function ? Function : "anonymous", Line, Exception ? Exception : "empty exception").R();
								
								if (Base->Route->Gateway.ReportStack)
								{
									Script::VMContext* Context = Gateway->GetContext();
									if (Context != nullptr)
									{
										std::string Stack = Context->GetErrorStackTrace();
										if (!Stack.empty())
											Base->Info.Message += "\n\n" + Stack;
									}
								}
							}
							else
								Base->Info.Message.assign("Internal processing error occurred.");
						};
						Base->Gateway->E.Finish = [Base](GatewayFrame* Gateway)
						{
							if (Base->WebSocket != nullptr)
							{
								if ((Base->WebSocket->State == (uint32_t)WebSocketState::Receive || Base->WebSocket->State == (uint32_t)WebSocketState::Process || Base->WebSocket->State == (uint32_t)WebSocketState::Open) && (Base->WebSocket->Connect || Base->WebSocket->Disconnect || Base->WebSocket->Notification || Base->WebSocket->Receive))
									Base->WebSocket->Next();
								else
									Base->WebSocket->Finish();
							}
							else
								Gateway->Finish();
						};
						Base->Gateway->E.Close = [Base](GatewayFrame*)
						{
							if (Base->Response.StatusCode <= 0)
								Base->Response.StatusCode = 200;

							if (!Base->WebSocket)
								return Base->Finish();

							Base->WebSocket->Finish();
							return true;
						};
						Base->Gateway->Start(Base->Request.Path, Base->Request.Method, Buffer, Size);
					});
				});
			}
			bool Util::ProcessWebSocket(Connection* Base, const char* Key)
			{
				TH_ASSERT(Base != nullptr, false, "connection should be set");
				TH_ASSERT(Key != nullptr, false, "key should be set");

				const char* Version = Base->Request.GetHeader("Sec-WebSocket-Version");
				if (!Base->Route || !Version || strcmp(Version, "13") != 0)
					return Base->Error(426, "Protocol upgrade required. Version \"%s\" is not allowed", Version);

				char Buffer[100];
				snprintf(Buffer, sizeof(Buffer), "%s%s", Key, WEBSOCKET_KEY);
				Base->Request.Buffer.clear();

				char Encoded20[20];
				Compute::Common::Sha1Compute(Buffer, (int)strlen(Buffer), (unsigned char*)Encoded20);

				Core::Parser Content;
				Content.fAppend(
					"HTTP/1.1 101 Switching Protocols\r\n"
					"Upgrade: websocket\r\n"
					"Connection: Upgrade\r\n"
					"Sec-WebSocket-Accept: %s\r\n", Compute::Common::Base64Encode((const unsigned char*)Encoded20, 20).c_str());

				const char* Protocol = Base->Request.GetHeader("Sec-WebSocket-Protocol");
				if (Protocol != nullptr)
				{
					const char* Offset = strchr(Protocol, ',');
					if (Offset != nullptr)
						Content.fAppend("Sec-WebSocket-Protocol: %.*s\r\n", (int)(Offset - Protocol), Protocol);
					else
						Content.fAppend("Sec-WebSocket-Protocol: %s\r\n", Protocol);
				}

				if (Base->Route->Callbacks.Headers)
					Base->Route->Callbacks.Headers(Base, &Content);

				Content.Append("\r\n", 2);
				return !Base->Stream->WriteAsync(Content.Get(), (int64_t)Content.Size(), [Base](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						Base->WebSocket = TH_NEW(WebSocketFrame, Base->Stream);
						Base->WebSocket->Connect = Base->Route->Callbacks.WebSocket.Connect;
						Base->WebSocket->Receive = Base->Route->Callbacks.WebSocket.Receive;
						Base->WebSocket->Disconnect = Base->Route->Callbacks.WebSocket.Disconnect;
						Base->WebSocket->E.Dead = [Base](WebSocketFrame*)
						{
							return Base->Info.Close;
						};
						Base->WebSocket->E.Reset = [Base](WebSocketFrame*)
						{
							Base->Break();
						};
						Base->WebSocket->E.Close = [Base](WebSocketFrame*)
						{
							Base->Info.KeepAlive = 0;
							if (Base->Response.StatusCode <= 0)
								Base->Response.StatusCode = 101;
							Base->Finish();
						};

						Base->Stream->SetAsyncTimeout(Base->Route->WebSocketTimeout);
						if (!ResourceProvided(Base, &Base->Resource))
							Base->WebSocket->Next();
						else
							ProcessGateway(Base);
					}
					else if (Packet::IsError(Event))
						Base->Break();
				});
			}

			Server::Server() : SocketServer()
			{
			}
			Server::~Server()
			{
				Unlisten();
			}
			bool Server::OnConfigure(SocketRouter* NewRouter)
			{
				TH_ASSERT(NewRouter != nullptr, false, "router should be set");
				std::unordered_set<std::string> Modules;
				std::string Directory = Core::OS::Directory::Get();
				auto* Root = (MapRouter*)NewRouter;

				Root->ModuleRoot = Core::OS::Path::ResolveDirectory(Root->ModuleRoot.c_str());
				for (auto& Site : Root->Sites)
				{
					auto* Entry = Site.second;
					Entry->Gateway.Session.DocumentRoot = Core::OS::Path::ResolveDirectory(Entry->Gateway.Session.DocumentRoot.c_str());
					Entry->ResourceRoot = Core::OS::Path::ResolveDirectory(Entry->ResourceRoot.c_str());
					Entry->Base->DocumentRoot = Core::OS::Path::ResolveDirectory(Entry->Base->DocumentRoot.c_str());
					Entry->Base->URI = Compute::RegexSource("/");
					Entry->Base->Site = Entry;
					Entry->Router = Root;

					if (!Entry->Gateway.Session.DocumentRoot.empty())
						Core::OS::Directory::Patch(Entry->Gateway.Session.DocumentRoot);

					if (!Entry->ResourceRoot.empty())
						Core::OS::Directory::Patch(Entry->ResourceRoot);

					if (!Entry->Base->Override.empty())
						Entry->Base->Override = Core::OS::Path::ResolveResource(Entry->Base->Override, Entry->Base->DocumentRoot);

					for (auto& Item : Entry->Base->ErrorFiles)
						Item.Pattern = Core::OS::Path::Resolve(Item.Pattern.c_str());

					for (auto& Group : Entry->Groups)
					{
						for (auto* Route : Group.Routes)
						{
							Route->DocumentRoot = Core::OS::Path::ResolveDirectory(Route->DocumentRoot.c_str());
							Route->Site = Entry;

							if (!Route->Override.empty())
								Route->Override = Core::OS::Path::ResolveResource(Route->Override, Route->DocumentRoot);

							for (auto& File : Route->ErrorFiles)
								File.Pattern = Core::OS::Path::Resolve(File.Pattern.c_str());

							if (!Root->VM || !Entry->Gateway.Enabled || !Entry->Gateway.Verify)
								continue;

							if (Modules.find(Route->DocumentRoot) != Modules.end())
								continue;

							Modules.insert(Route->DocumentRoot);
							for (auto& Exp : Route->Gateway.Files)
							{
								std::vector<std::string> Result = Root->VM->VerifyModules(Route->DocumentRoot, Exp);
								if (!Result.empty())
								{
									std::string Files;
									for (auto& Name : Result)
										Files += "\n\t" + Name;

									TH_ERR("(vm) there are errors in %i module(s)%s", (int)Result.size(), Files.c_str());
									Entry->Gateway.Enabled = false;
									break;
								}
							}

							if (Entry->Gateway.Enabled && !Route->Gateway.Files.empty())
								TH_TRACE("(vm) modules are verified for: %s", Route->DocumentRoot.c_str());
						}
					}

					Entry->Sort();
				}

				return true;
			}
			bool Server::OnRequestEnded(SocketConnection* Root, bool Check)
			{
				TH_ASSERT(Root != nullptr, false, "connection should be set");
				if (Check)
					return true;

				auto Base = (HTTP::Connection*)Root;
				for (auto& Item : Base->Request.Resources)
				{
					if (!Item.Memory)
						Core::OS::File::Remove(Item.Path.c_str());
				}

				if (Base->Info.KeepAlive >= -1 && Base->Response.StatusCode >= 0 && Base->Route && Base->Route->Callbacks.Access)
					Base->Route->Callbacks.Access(Base);

				Base->Route = nullptr;
				Base->Stream->Income = 0;
				Base->Stream->Outcome = 0;
				Base->Info.Close = (Base->Info.Close || Base->Response.StatusCode < 0);
				Base->Response.Data = Content::Not_Loaded;
				Base->Response.Error = false;
				Base->Response.StatusCode = -1;
				Base->Response.Buffer.clear();
				Base->Response.Cookies.clear();
				Base->Response.Headers.clear();
				Base->Request.ContentLength = 0;
				Base->Request.User.Type = Auth::Unverified;
				Base->Request.User.Username.clear();
				Base->Request.User.Password.clear();
				Base->Request.Resources.clear();
				Base->Request.Buffer.clear();
				Base->Request.Headers.clear();
				Base->Request.Cookies.clear();
				Base->Request.Query.clear();
				Base->Request.Path.clear();
				Base->Request.URI.clear();
				Base->Request.Where.clear();

				memset(Base->Request.Method, 0, sizeof(Base->Request.Method));
				memset(Base->Request.Version, 0, sizeof(Base->Request.Version));
				memset(Base->Request.RemoteAddress, 0, sizeof(Base->Request.RemoteAddress));

				return true;
			}
			bool Server::OnRequestBegin(SocketConnection* Source)
			{
				TH_ASSERT(Source != nullptr, false, "connection should be set");
				auto* Conf = (MapRouter*)Router;
				auto* Base = (Connection*)Source;

				return Base->Stream->ReadUntilAsync("\r\n\r\n", [Base, Conf](NetEvent Event, const char* Buffer, size_t Recv)
				{
					if (Packet::IsData(Event))
					{
						Base->Request.Buffer.append(Buffer, Recv);
						return true;
					}
					else if (Packet::IsDone(Event))
					{
						ParserFrame Segment;
						Segment.Request = &Base->Request;

						HTTP::Parser* Parser = new HTTP::Parser();
						Parser->OnMethodValue = Util::ParseMethodValue;
						Parser->OnPathValue = Util::ParsePathValue;
						Parser->OnQueryValue = Util::ParseQueryValue;
						Parser->OnVersion = Util::ParseVersion;
						Parser->OnHeaderField = Util::ParseHeaderField;
						Parser->OnHeaderValue = Util::ParseHeaderValue;
						Parser->UserPointer = &Segment;

						strcpy(Base->Request.RemoteAddress, Base->Stream->GetRemoteAddress().c_str());
						Base->Info.Start = Driver::Clock();

						if (Parser->ParseRequest(Base->Request.Buffer.c_str(), Base->Request.Buffer.size(), 0) < 0)
						{
							Base->Request.Buffer.clear();
							TH_RELEASE(Parser);

							return Base->Error(400, "Invalid request was provided by client");
						}

						Base->Request.Buffer.clear();
						TH_RELEASE(Parser);

						if (!Util::ConstructRoute(Conf, Base) || !Base->Route)
							return Base->Error(400, "Request cannot be resolved");

						if (!Base->Route->Redirect.empty())
						{
							Base->Request.URI = Base->Route->Redirect;
							if (!Util::ConstructRoute(Conf, Base))
								Base->Route = Base->Route->Site->Base;
						}

						const char* ContentLength = Base->Request.GetHeader("Content-Length");
						if (ContentLength != nullptr)
						{
							int64_t Len = std::atoll(ContentLength);
							Base->Request.ContentLength = (Len <= 0 ? 0 : Len);
						}

						if (!Base->Request.ContentLength)
							Base->Response.Data = Content::Empty;

						if (!Base->Route->ProxyIpAddress.empty())
						{
							const char* Address = Base->Request.GetHeader(Base->Route->ProxyIpAddress.c_str());
							if (Address != nullptr)
								strcpy(Base->Request.RemoteAddress, Address);
						}

						Util::ConstructPath(Base);
						if (!Util::MethodAllowed(Base))
							return Base->Error(405, "Requested method \"%s\" is not allowed on this server", Base->Request.Method);

						if (!Util::Authorize(Base))
							return false;

						if (!memcmp(Base->Request.Method, "GET", 3) || !memcmp(Base->Request.Method, "HEAD", 4))
						{
							if (Base->Route->Callbacks.Get)
								return Base->Route->Callbacks.Get(Base);

							return Util::RouteGET(Base);
						}
						else if (!memcmp(Base->Request.Method, "POST", 4))
						{
							if (Base->Route->Callbacks.Post)
								return Base->Route->Callbacks.Post(Base);

							return Util::RoutePOST(Base);
						}
						else if (!memcmp(Base->Request.Method, "PUT", 3))
						{
							if (Base->Route->Callbacks.Put)
								return Base->Route->Callbacks.Put(Base);

							return Util::RoutePUT(Base);
						}
						else if (!memcmp(Base->Request.Method, "PATCH", 5))
						{
							if (Base->Route->Callbacks.Patch)
								return Base->Route->Callbacks.Patch(Base);

							return Util::RoutePATCH(Base);
						}
						else if (!memcmp(Base->Request.Method, "DELETE", 6))
						{
							if (Base->Route->Callbacks.Delete)
								return Base->Route->Callbacks.Delete(Base);

							return Util::RouteDELETE(Base);
						}
						else if (!memcmp(Base->Request.Method, "OPTIONS", 7))
						{
							if (Base->Route->Callbacks.Options)
								return Base->Route->Callbacks.Options(Base);

							return Util::RouteOPTIONS(Base);
						}

						return Base->Error(405, "Request method \"%s\" is not allowed", Base->Request.Method);
					}
					else if (Packet::IsError(Event))
						Base->Break();

					return true;
				});
			}
			bool Server::OnDeallocate(SocketConnection* Base)
			{
				HTTP::Connection* sBase = (HTTP::Connection*)Base;
				TH_DELETE(Connection, sBase);
				return true;
			}
			bool Server::OnDeallocateRouter(SocketRouter* Base)
			{
				HTTP::MapRouter* sBase = (HTTP::MapRouter*)Base;
				TH_DELETE(MapRouter, sBase);
				return true;
			}
			bool Server::OnListen()
			{
				return true;
			}
			bool Server::OnUnlisten()
			{
				TH_ASSERT(Router != nullptr, false, "router should be set");
				MapRouter* Root = (MapRouter*)Router;

				for (auto& Site : Root->Sites)
				{
					auto* Entry = Site.second;
					if (!Entry->ResourceRoot.empty())
					{
						if (!Core::OS::Directory::Remove(Entry->ResourceRoot.c_str()))
							TH_ERR("resource directory %s cannot be deleted", Entry->ResourceRoot.c_str());

						if (!Core::OS::Directory::Create(Entry->ResourceRoot.c_str()))
							TH_ERR("resource directory %s cannot be created", Entry->ResourceRoot.c_str());
					}

					if (!Entry->Gateway.Session.DocumentRoot.empty())
						Session::InvalidateCache(Entry->Gateway.Session.DocumentRoot);
				}

				return true;
			}
			SocketConnection* Server::OnAllocate(Listener* Host, Socket* Stream)
			{
				TH_ASSERT(Host != nullptr, nullptr, "host should be set");
				TH_ASSERT(Stream != nullptr, nullptr, "host should be set");

				auto Base = TH_NEW(HTTP::Connection);
				Base->Root = this;

				return Base;
			}
			SocketRouter* Server::OnAllocateRouter()
			{
				return TH_NEW(MapRouter);
			}

			Client::Client(int64_t ReadTimeout) : SocketClient(ReadTimeout), WebSocket(nullptr)
			{
			}
			Client::~Client()
			{
				TH_DELETE(WebSocketFrame, WebSocket);
			}
			bool Client::Downgrade()
			{
				TH_ASSERT(WebSocket != nullptr, false, "websocket should be opened");
				TH_ASSERT(WebSocket->IsFinished(), false, "websocket connection should be finished");

				TH_DELETE(WebSocketFrame, WebSocket);
				WebSocket = nullptr;

				return true;
			}
			Core::Async<bool> Client::Consume(int64_t MaxSize)
			{
				TH_ASSERT(!WebSocket, false, "cannot read http over websocket");
				if (Response.HasBody())
					return true;

				if (Response.Data == Content::Lost || Response.Data == Content::Wants_Save || Response.Data == Content::Corrupted || Response.Data == Content::Payload_Exceeded || Response.Data == Content::Save_Exception)
					return false;

				Response.Buffer.clear();
				if (!Stream.IsValid())
					return false;

				const char* ContentType = Response.GetHeader("Content-Type");
				if (ContentType && !strncmp(ContentType, "multipart/form-data", 19))
				{
					Response.Data = Content::Wants_Save;
					return false;
				}

				const char* TransferEncoding = Response.GetHeader("Transfer-Encoding");
				if (TransferEncoding && !Core::Parser::CaseCompare(TransferEncoding, "chunked"))
				{
					Core::Async<bool> Result;
					Parser* Parser = new HTTP::Parser();
					Stream.ReadAsync(MaxSize, [this, Parser, Result, MaxSize](NetEvent Event, const char* Buffer, size_t Recv) mutable
					{
						if (Packet::IsData(Event))
						{
							int64_t Subresult = Parser->ParseDecodeChunked((char*)Buffer, &Recv);
							if (Subresult == -1)
							{
								TH_RELEASE(Parser);
								Response.Data = Content::Corrupted;
								Result = false;

								return false;
							}
							else if (Subresult >= 0 || Subresult == -2)
							{
								if (Response.Buffer.size() < MaxSize)
									TextAppend(Response.Buffer, Buffer, Recv);
							}

							return Subresult == -2;
						}
						else if (Packet::IsDone(Event))
						{
							if (Response.Buffer.size() < MaxSize)
								Response.Data = Content::Cached;
							else
								Response.Data = Content::Lost;
						}
						else if (Packet::IsErrorOrSkip(Event))
							Response.Data = Content::Corrupted;

						TH_RELEASE(Parser);
						if (!Response.Buffer.empty())
							TH_TRACE("[http] %i responded\n%.*s", (int)Stream.GetFd(), (int)Response.Buffer.size(), Response.Buffer.data());
						Result = Response.HasBody();
						return true;
					});

					return Result;
				}
				else if (!Response.GetHeader("Content-Length"))
				{
					Core::Async<bool> Result;
					Stream.ReadAsync(MaxSize, [this, Result, MaxSize](NetEvent Event, const char* Buffer, size_t Recv) mutable
					{
						if (Packet::IsData(Event))
						{
							if (Response.Buffer.size() < MaxSize)
								TextAppend(Response.Buffer, Buffer, Recv);

							return true;
						}
						else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
						{
							if (Response.Buffer.size() < MaxSize)
								Response.Data = Content::Cached;
							else
								Response.Data = Content::Lost;
						}

						if (!Response.Buffer.empty())
							TH_TRACE("[http] %i responded\n%.*s", (int)Stream.GetFd(), (int)Response.Buffer.size(), Response.Buffer.data());

						Result = Response.HasBody();
						return false;
					});

					return Result;
				}

				const char* HContentLength = Response.GetHeader("Content-Length");
				if (!HContentLength)
				{
					Response.Data = Content::Corrupted;
					return false;
				}

				Core::Parser HLength = HContentLength;
				if (!HLength.HasInteger())
				{
					Response.Data = Content::Corrupted;
					return false;
				}

				int64_t Length = HLength.ToInt64();
				if (Length <= 0)
				{
					Response.Data = Content::Empty;
					return true;
				}

				if (Length > MaxSize)
				{
					Response.Data = Content::Wants_Save;
					return false;
				}

				Core::Async<bool> Result;
				Stream.ReadAsync(Length, [this, Result, MaxSize](NetEvent Event, const char* Buffer, size_t Recv) mutable
				{
					if (Packet::IsData(Event))
					{
						if (Response.Buffer.size() < MaxSize)
							TextAppend(Response.Buffer, Buffer, Recv);

						return true;
					}
					else if (Packet::IsDone(Event) || Packet::IsErrorOrSkip(Event))
					{
						if (Response.Buffer.size() < MaxSize)
							Response.Data = Content::Cached;
						else
							Response.Data = Content::Lost;
					}

					if (!Response.Buffer.empty())
						TH_TRACE("[http] %i responded\n%.*s", (int)Stream.GetFd(), (int)Response.Buffer.size(), Response.Buffer.data());

					Result = Response.HasBody();
					return false;
				});

				return Result;
			}
			Core::Async<bool> Client::Fetch(HTTP::RequestFrame&& Root, int64_t MaxSize)
			{
				return Send(std::move(Root)).Then<Core::Async<bool>>([this, MaxSize](HTTP::ResponseFrame*&&)
				{
					return Consume(MaxSize);
				});
			}
			Core::Async<bool> Client::Upgrade(HTTP::RequestFrame&& Root)
			{
				TH_ASSERT(WebSocket != nullptr, false, "websocket should be opened");
				TH_ASSERT(Stream.IsValid(), false, "stream should be opened");

				std::string Key = Compute::Common::Base64Encode(Compute::Common::RandomBytes(16));
				Root.SetHeader("Pragma", "no-cache");
				Root.SetHeader("Upgrade", "WebSocket");
				Root.SetHeader("Connection", "Upgrade");
				Root.SetHeader("Sec-WebSocket-Key", Key);
				Root.SetHeader("Sec-WebSocket-Version", "13");

				return Send(std::move(Root)).Then<Core::Async<bool>>([this](ResponseFrame*&& Response)
				{
					TH_TRACE("[ws] handshake %s", Request.URI.c_str());
					if (Response->StatusCode != 101)
						return Core::Async<bool>(Error("ws handshake error") && false);

					if (!Response->GetHeader("Sec-WebSocket-Accept"))
						return Core::Async<bool>(Error("ws handshake was not accepted") && false);

					Future = Core::Async<bool>();
					WebSocket->Next();
					return Future;
				});
			}
			Core::Async<ResponseFrame*> Client::Send(HTTP::RequestFrame&& Root)
			{
				TH_ASSERT(!WebSocket || Root.GetHeader("Sec-WebSocket-Key") != nullptr, nullptr, "cannot send http request over websocket");
				TH_ASSERT(Stream.IsValid(), nullptr, "stream should be opened");
				TH_TRACE("[http] %s %s", Root.Method, Root.URI.c_str());

				Core::Async<ResponseFrame*> Result;
				Request = std::move(Root);
				Response.Data = Content::Not_Loaded;
				Done = [Result](SocketClient* Client, int Code) mutable
				{
					HTTP::Client* Base = (HTTP::Client*)Client;
					if (Code < 0)
						Base->GetResponse()->StatusCode = -1;

					Result = Base->GetResponse();
				};
				Stage("request delivery");

				Core::Parser Content;
				if (!Request.GetHeader("Host"))
				{
					if (Context != nullptr)
					{
						if (Hostname.Port == 443)
							Request.SetHeader("Host", Hostname.Hostname.c_str());
						else
							Request.SetHeader("Host", (Hostname.Hostname + ':' + std::to_string(Hostname.Port)));
					}
					else
					{
						if (Hostname.Port == 80)
							Request.SetHeader("Host", Hostname.Hostname);
						else
							Request.SetHeader("Host", (Hostname.Hostname + ':' + std::to_string(Hostname.Port)));
					}
				}

				if (!Request.GetHeader("Accept"))
					Request.SetHeader("Accept", "*/*");

				if (!Request.GetHeader("User-Agent"))
					Request.SetHeader("User-Agent", "Lynx/1.1");

				if (!Request.GetHeader("Content-Length"))
				{
					Request.ContentLength = Request.Buffer.size();
					Request.SetHeader("Content-Length", std::to_string(Request.Buffer.size()));
				}

				if (!Request.GetHeader("Connection"))
					Request.SetHeader("Connection", "Keep-Alive");

				if (!Request.Buffer.empty())
				{
					if (!Request.GetHeader("Content-Type"))
						Request.SetHeader("Content-Type", "application/octet-stream");

					if (!Request.GetHeader("Content-Length"))
						Request.SetHeader("Content-Length", std::to_string(Request.Buffer.size()).c_str());
				}
				else if (!memcmp(Request.Method, "POST", 4) || !memcmp(Request.Method, "PUT", 3) || !memcmp(Request.Method, "PATCH", 5))
					Request.SetHeader("Content-Length", "0");

				if (!Request.Query.empty())
					Content.fAppend("%s %s?%s %s\r\n", Request.Method, Request.URI.c_str(), Request.Query.c_str(), Request.Version);
				else
					Content.fAppend("%s %s %s\r\n", Request.Method, Request.URI.c_str(), Request.Version);

				Util::ConstructHeadFull(&Request, &Response, true, &Content);
				Content.Append("\r\n");

				Response.Buffer.clear();
				Stream.WriteAsync(Content.Get(), (int64_t)Content.Size(), [this](NetEvent Event, size_t Sent)
				{
					if (Packet::IsDone(Event))
					{
						if (!Request.Buffer.empty())
						{
							Stream.WriteAsync(Request.Buffer.c_str(), (int64_t)Request.Buffer.size(), [this](NetEvent Event, size_t Sent)
							{
								if (Packet::IsDone(Event))
								{
									Stream.ReadUntilAsync("\r\n\r\n", [this](NetEvent Event, const char* Buffer, size_t Recv)
									{
										if (Packet::IsData(Event))
											TextAppend(Response.Buffer, Buffer, Recv);
										else if (Packet::IsDone(Event))
											Receive();
										else if (Packet::IsErrorOrSkip(Event))
											Error("http socket read %s", (Event == NetEvent::Timeout ? "timeout" : "error"));

										return true;
									});
								}
								else if (Packet::IsErrorOrSkip(Event))
									Error("http socket write %s", (Event == NetEvent::Timeout ? "timeout" : "error"));
							});
						}
						else
						{
							Stream.ReadUntilAsync("\r\n\r\n", [this](NetEvent Event, const char* Buffer, size_t Recv)
							{
								if (Packet::IsData(Event))
									TextAppend(Response.Buffer, Buffer, Recv);
								else if (Packet::IsDone(Event))
									Receive();
								else if (Packet::IsErrorOrSkip(Event))
									Error("http socket read %s", (Event == NetEvent::Timeout ? "timeout" : "error"));

								return true;
							});
						}
					}
					else if (Packet::IsErrorOrSkip(Event))
						Error("http socket write %s", (Event == NetEvent::Timeout ? "timeout" : "error"));
				});

				return Result;
			}
			Core::Async<Core::Document*> Client::JSON(HTTP::RequestFrame&& Root, int64_t MaxSize)
			{
				return Fetch(std::move(Root), MaxSize).Then<Core::Document*>([this](bool&& Result)
				{
					if (!Result)
						return (Core::Document*)nullptr;

					return Core::Document::ReadJSON((int64_t)Response.Buffer.size(), [this](char* Buffer, int64_t Size)
					{
						memcpy(Buffer, Response.Buffer.data(), (size_t)Size);
						return true;
					});
				});
			}
			Core::Async<Core::Document*> Client::XML(HTTP::RequestFrame&& Root, int64_t MaxSize)
			{
				return Fetch(std::move(Root), MaxSize).Then<Core::Document*>([this](bool&& Result)
				{
					if (!Result)
						return (Core::Document*)nullptr;

					return Core::Document::ReadXML((int64_t)Response.Buffer.size(), [this](char* Buffer, int64_t Size)
					{
						memcpy(Buffer, Response.Buffer.data(), (size_t)Size);
						return true;
					});
				});
			}
			WebSocketFrame* Client::GetWebSocket()
			{
				if (WebSocket != nullptr)
					return WebSocket;

				WebSocket = TH_NEW(WebSocketFrame, &Stream);
				WebSocket->E.Dead = [](WebSocketFrame*)
				{
					return false;
				};
				WebSocket->E.Reset = [this](WebSocketFrame*)
				{
					Stream.Close(false);
				};
				WebSocket->E.Close = [this](WebSocketFrame*)
				{
					Future = true;
				};

				return WebSocket;
			}
			RequestFrame* Client::GetRequest()
			{
				return &Request;
			}
			ResponseFrame* Client::GetResponse()
			{
				return &Response;
			}
			bool Client::Receive()
			{
				ParserFrame Segment;
				Segment.Response = &Response;
				Stage("http response receive");

				Parser* Parser = new HTTP::Parser();
				Parser->OnMethodValue = Util::ParseMethodValue;
				Parser->OnPathValue = Util::ParsePathValue;
				Parser->OnQueryValue = Util::ParseQueryValue;
				Parser->OnVersion = Util::ParseVersion;
				Parser->OnStatusCode = Util::ParseStatusCode;
				Parser->OnHeaderField = Util::ParseHeaderField;
				Parser->OnHeaderValue = Util::ParseHeaderValue;
				Parser->UserPointer = &Segment;

				strcpy(Request.RemoteAddress, Stream.GetRemoteAddress().c_str());
				if (Parser->ParseResponse(Response.Buffer.data(), Response.Buffer.size(), 0) < 0)
				{
					TH_RELEASE(Parser);
					return Error("cannot parse http response");
				}

				TH_RELEASE(Parser);
				return Success(0);
			}
		}
	}
}
