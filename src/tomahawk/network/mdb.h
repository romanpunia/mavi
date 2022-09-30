#ifndef TH_NETWORK_MONGODB_H
#define TH_NETWORK_MONGODB_H
#include "../core/network.h"

struct _bson_t;
struct _mongoc_client_t;
struct _mongoc_uri_t;
struct _mongoc_database_t;
struct _mongoc_collection_t;
struct _mongoc_cursor_t;
struct _mongoc_bulk_operation_t;
struct _mongoc_client_pool_t;
struct _mongoc_change_stream_t;
struct _mongoc_client_session_t;

namespace Tomahawk
{
	namespace Network
	{
		namespace MDB
		{
			typedef _bson_t TDocument;
			typedef _mongoc_client_pool_t TConnectionPool;
			typedef _mongoc_bulk_operation_t TStream;
			typedef _mongoc_cursor_t TCursor;
			typedef _mongoc_collection_t TCollection;
			typedef _mongoc_database_t TDatabase;
			typedef _mongoc_uri_t TAddress;
			typedef _mongoc_client_t TConnection;
			typedef _mongoc_change_stream_t TWatcher;
			typedef _mongoc_client_session_t TTransaction;
			typedef std::function<void(const std::string&)> OnQueryLog;

			class Transaction;

			class Connection;

			class Cluster;

			class Schema;

			enum class QueryType
			{

			};

			enum class QueryFlags
			{
				None = 0,
				Tailable_Cursor = 1 << 1,
				Slave_Ok = 1 << 2,
				Oplog_Replay = 1 << 3,
				No_Cursor_Timeout = 1 << 4,
				Await_Data = 1 << 5,
				Exhaust = 1 << 6,
				Partial = 1 << 7,
			};

			enum class Type
			{
				Unknown,
				Uncastable,
				Null,
				Schema,
				Array,
				String,
				Integer,
				Number,
				Boolean,
				ObjectId,
				Decimal
			};

			enum class TransactionState
			{
				OK,
				Retry_Commit,
				Retry,
				Fatal
			};

			inline QueryFlags operator |(QueryFlags A, QueryFlags B)
			{
				return static_cast<QueryFlags>(static_cast<uint64_t>(A) | static_cast<uint64_t>(B));
			}

			struct TH_OUT Property
			{
				std::string Name;
				std::string String;
				TDocument* Source;
				Type Mod;
				int64_t Integer;
				uint64_t High;
				uint64_t Low;
				double Number;
				unsigned char ObjectId[12] = { 0 };
				bool Boolean;
				bool IsValid;

				Property() noexcept;
				Property(const Property& Other) noexcept;
				Property(Property&& Other) noexcept;
				~Property();
				void Release();
				std::string& ToString();
				TDocument* GetOwnership();
				Schema Get() const;
				Property& operator= (const Property& Other) noexcept;
				Property& operator= (Property&& Other) noexcept;
				Property operator [](const char* Name);
				Property operator [](const char* Name) const;
			};

			class TH_OUT Util
			{
			public:
				static bool GetId(unsigned char* Id12);
				static bool GetDecimal(const char* Value, int64_t* High, int64_t* Low);
				static unsigned int GetHashId(unsigned char* Id12);
				static int64_t GetTimeId(unsigned char* Id12);
				static std::string IdToString(unsigned char* Id12);
				static std::string StringToId(const std::string& Id24);
			};

			class TH_OUT Schema
			{
			private:
				TDocument* Base;
				bool Store;

			public:
				Schema();
				Schema(TDocument* NewBase);
				void Release() const;
				void Release();
				void Join(const Schema& Value);
				void Loop(const std::function<bool(Property*)>& Callback) const;
				bool SetSchema(const char* Key, const Schema& Value, uint64_t ArrayId = 0);
				bool SetArray(const char* Key, const Schema& Array, uint64_t ArrayId = 0);
				bool SetString(const char* Key, const char* Value, uint64_t ArrayId = 0);
				bool SetBlob(const char* Key, const char* Value, uint64_t Length, uint64_t ArrayId = 0);
				bool SetInteger(const char* Key, int64_t Value, uint64_t ArrayId = 0);
				bool SetNumber(const char* Key, double Value, uint64_t ArrayId = 0);
				bool SetDecimal(const char* Key, uint64_t High, uint64_t Low, uint64_t ArrayId = 0);
				bool SetDecimalString(const char* Key, const std::string& Value, uint64_t ArrayId = 0);
				bool SetDecimalInteger(const char* Key, int64_t Value, uint64_t ArrayId = 0);
				bool SetDecimalNumber(const char* Key, double Value, uint64_t ArrayId = 0);
				bool SetBoolean(const char* Key, bool Value, uint64_t ArrayId = 0);
				bool SetObjectId(const char* Key, unsigned char Value[12], uint64_t ArrayId = 0);
				bool SetNull(const char* Key, uint64_t ArrayId = 0);
				bool SetProperty(const char* Key, Property* Value, uint64_t ArrayId = 0);
				bool HasProperty(const char* Key) const;
				bool GetProperty(const char* Key, Property* Output) const;
				uint64_t Count() const;
				std::string ToRelaxedJSON() const;
				std::string ToExtendedJSON() const;
				std::string ToJSON() const;
				std::string ToIndices() const;
				Core::Schema* ToSchema(bool IsArray = false) const;
				TDocument* Get() const;
				Schema Copy() const;
				Schema& Persist(bool Keep = true);
				operator bool() const
				{
					return Base != nullptr;
				}
				Property operator [](const char* Name)
				{
					Property Result;
					GetProperty(Name, &Result);
					return Result;
				}
				Property operator [](const char* Name) const
				{
					Property Result;
					GetProperty(Name, &Result);
					return Result;
				}

			public:
				static Schema FromEmpty();
				static Schema FromDocument(Core::Schema* Schema);
				static Schema FromJSON(const std::string& JSON);
				static Schema FromBuffer(const unsigned char* Buffer, uint64_t Length);
				static Schema FromSource(TDocument* Src);

			private:
				static bool Clone(void* It, Property* Output);
			};

			class TH_OUT Address
			{
			private:
				TAddress* Base;

			public:
				Address(TAddress* NewBase);
				void Release();
				void SetOption(const char* Name, int64_t Value);
				void SetOption(const char* Name, bool Value);
				void SetOption(const char* Name, const char* Value);
				void SetAuthMechanism(const char* Value);
				void SetAuthSource(const char* Value);
				void SetCompressors(const char* Value);
				void SetDatabase(const char* Value);
				void SetUsername(const char* Value);
				void SetPassword(const char* Value);
				TAddress* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}

			public:
				static Address FromURI(const char* Value);
			};

			class TH_OUT Stream
			{
			private:
				Schema IOptions;
				TCollection* Source;
				TStream* Base;
				size_t Count;

			public:
				Stream();
				Stream(TCollection* NewSource, TStream* NewBase, const Schema& NewOptions);
				void Release();
				bool RemoveMany(const Schema& Match, const Schema& Options);
				bool RemoveOne(const Schema& Match, const Schema& Options);
				bool ReplaceOne(const Schema& Match, const Schema& Replacement, const Schema& Options);
				bool InsertOne(const Schema& Result, const Schema& Options);
				bool UpdateOne(const Schema& Match, const Schema& Result, const Schema& Options);
				bool UpdateMany(const Schema& Match, const Schema& Result, const Schema& Options);
				bool TemplateQuery(const std::string& Name, Core::SchemaArgs* Map, bool Once = true);
				bool Query(const Schema& Command);
				Core::Async<Schema> ExecuteWithReply();
				Core::Async<bool> Execute();
				uint64_t GetHint() const;
				TStream* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}

			private:
				bool NextOperation();
			};

			class TH_OUT Cursor
			{
			private:
				TCursor* Base;

			public:
				Cursor();
				Cursor(TCursor* NewBase);
				void Release();
				void SetMaxAwaitTime(uint64_t MaxAwaitTime);
				void SetBatchSize(uint64_t BatchSize);
				bool SetLimit(int64_t Limit);
				bool SetHint(uint64_t Hint);
				bool HasError() const;
				bool HasMoreData() const;
				Core::Async<bool> Next() const;
				int64_t GetId() const;
				int64_t GetLimit() const;
				uint64_t GetMaxAwaitTime() const;
				uint64_t GetBatchSize() const;
				uint64_t GetHint() const;
				Schema GetCurrent() const;
				Cursor Clone();
				TCursor* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}
			};

			class TH_OUT Response
			{
			private:
				Cursor ICursor;
				Schema IDocument;
				bool ISuccess;

			public:
				Response();
				Response(const Response& Other);
				Response(const Cursor& _Cursor);
				Response(const Schema& _Document);
				Response(bool _Success);
				void Release();
				Core::Async<Core::Schema*> Fetch() const;
				Core::Async<Core::Schema*> FetchAll() const;
				Property GetProperty(const char* Name);
				Cursor GetCursor() const;
				Schema GetDocument() const;
				bool IsOK() const;
				bool OK();
				Property operator [](const char* Name)
				{
					return GetProperty(Name);
				}
				operator bool() const
				{
					return IsOK();
				}
			};

			class TH_OUT Collection
			{
			private:
				TCollection* Base;

			public:
				Collection();
				Collection(TCollection* NewBase);
				void Release();
				Core::Async<bool> Rename(const std::string& NewDatabaseName, const std::string& NewCollectionName);
				Core::Async<bool> RenameWithOptions(const std::string& NewDatabaseName, const std::string& NewCollectionName, const Schema& Options);
				Core::Async<bool> RenameWithRemove(const std::string& NewDatabaseName, const std::string& NewCollectionName);
				Core::Async<bool> RenameWithOptionsAndRemove(const std::string& NewDatabaseName, const std::string& NewCollectionName, const Schema& Options);
				Core::Async<bool> Remove(const Schema& Options);
				Core::Async<bool> RemoveIndex(const std::string& Name, const Schema& Options);
				Core::Async<Schema> RemoveMany(const Schema& Match, const Schema& Options);
				Core::Async<Schema> RemoveOne(const Schema& Match, const Schema& Options);
				Core::Async<Schema> ReplaceOne(const Schema& Match, const Schema& Replacement, const Schema& Options);
				Core::Async<Schema> InsertMany(std::vector<Schema>& List, const Schema& Options);
				Core::Async<Schema> InsertOne(const Schema& Result, const Schema& Options);
				Core::Async<Schema> UpdateMany(const Schema& Match, const Schema& Update, const Schema& Options);
				Core::Async<Schema> UpdateOne(const Schema& Match, const Schema& Update, const Schema& Options);
				Core::Async<Schema> FindAndModify(const Schema& Match, const Schema& Sort, const Schema& Update, const Schema& Fields, bool Remove, bool Upsert, bool New);
				Core::Async<uint64_t> CountDocuments(const Schema& Match, const Schema& Options) const;
				Core::Async<uint64_t> CountDocumentsEstimated(const Schema& Options) const;
				Core::Async<Cursor> FindIndexes(const Schema& Options) const;
				Core::Async<Cursor> FindMany(const Schema& Match, const Schema& Options) const;
				Core::Async<Cursor> FindOne(const Schema& Match, const Schema& Options) const;
				Core::Async<Cursor> Aggregate(QueryFlags Flags, const Schema& Pipeline, const Schema& Options) const;
				Core::Async<Response> TemplateQuery(const std::string& Name, Core::SchemaArgs* Map, bool Once = true, Transaction* Session = nullptr);
				Core::Async<Response> Query(const Schema& Command, Transaction* Session = nullptr);
				const char* GetName() const;
				Stream CreateStream(const Schema& Options);
				TCollection* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}
			};

			class TH_OUT Database
			{
			private:
				TDatabase* Base;

			public:
				Database(TDatabase* NewBase);
				void Release();
				Core::Async<bool> RemoveAllUsers();
				Core::Async<bool> RemoveUser(const std::string& Name);
				Core::Async<bool> Remove();
				Core::Async<bool> RemoveWithOptions(const Schema& Options);
				Core::Async<bool> AddUser(const std::string& Username, const std::string& Password, const Schema& Roles, const Schema& Custom);
				Core::Async<bool> HasCollection(const std::string& Name) const;
				Core::Async<Collection> CreateCollection(const std::string& Name, const Schema& Options);
				Core::Async<Cursor> FindCollections(const Schema& Options) const;
				std::vector<std::string> GetCollectionNames(const Schema& Options) const;
				const char* GetName() const;
				Collection GetCollection(const std::string& Name);
				TDatabase* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}
			};

			class TH_OUT Watcher
			{
			private:
				TWatcher* Base;

			public:
				Watcher(TWatcher* NewBase);
				void Release();
				Core::Async<bool> Next(const Schema& Result) const;
				Core::Async<bool> Error(const Schema& Result) const;
				TWatcher* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}

			public:
				static Watcher FromConnection(Connection* Connection, const Schema& Pipeline, const Schema& Options);
				static Watcher FromDatabase(const Database& Src, const Schema& Pipeline, const Schema& Options);
				static Watcher FromCollection(const Collection& Src, const Schema& Pipeline, const Schema& Options);
			};

			class TH_OUT Transaction
			{
			private:
				TTransaction* Base;

			public:
				Transaction(TTransaction* NewBase);
				bool Push(Schema& QueryOptions) const;
				bool Put(TDocument** QueryOptions) const;
				Core::Async<bool> Start();
				Core::Async<bool> Abort();
				Core::Async<Schema> RemoveMany(const Collection& Base, const Schema& Match, const Schema& Options);
				Core::Async<Schema> RemoveOne(const Collection& Base, const Schema& Match, const Schema& Options);
				Core::Async<Schema> ReplaceOne(const Collection& Base, const Schema& Match, const Schema& Replacement, const Schema& Options);
				Core::Async<Schema> InsertMany(const Collection& Base, std::vector<Schema>& List, const Schema& Options);
				Core::Async<Schema> InsertOne(const Collection& Base, const Schema& Result, const Schema& Options);
				Core::Async<Schema> UpdateMany(const Collection& Base, const Schema& Match, const Schema& Update, const Schema& Options);
				Core::Async<Schema> UpdateOne(const Collection& Base, const Schema& Match, const Schema& Update, const Schema& Options);
				Core::Async<Cursor> FindMany(const Collection& Base, const Schema& Match, const Schema& Options) const;
				Core::Async<Cursor> FindOne(const Collection& Base, const Schema& Match, const Schema& Options) const;
				Core::Async<Cursor> Aggregate(const Collection& Base, QueryFlags Flags, const Schema& Pipeline, const Schema& Options) const;
				Core::Async<Response> TemplateQuery(const Collection& Base, const std::string& Name, Core::SchemaArgs* Map, bool Once = true);
				Core::Async<Response> Query(const Collection& Base, const Schema& Command);
				Core::Async<TransactionState> Commit();
				TTransaction* Get() const;
				operator bool() const
				{
					return Base != nullptr;
				}
			};

			class TH_OUT Connection : public Core::Object
			{
				friend Cluster;
				friend Transaction;

			private:
				std::atomic<bool> Connected;
				Transaction Session;
				TConnection* Base;
				Cluster* Master;

			public:
				Connection();
				virtual ~Connection() override;
				Core::Async<bool> Connect(const std::string& Address);
				Core::Async<bool> Connect(Address* URI);
				Core::Async<bool> Disconnect();
				Core::Async<bool> MakeTransaction(const std::function<Core::Async<bool>(Transaction&)>& Callback);
				Core::Async<bool> MakeCotransaction(const std::function<bool(Transaction&)>& Callback);
				Core::Async<Cursor> FindDatabases(const Schema& Options) const;
				void SetProfile(const std::string& Name);
				bool SetServer(bool Writeable);
				Transaction GetSession();
				Database GetDatabase(const std::string& Name) const;
				Database GetDefaultDatabase() const;
				Collection GetCollection(const char* DatabaseName, const char* Name) const;
				Address GetAddress() const;
				Cluster* GetMaster() const;
				TConnection* Get() const;
				std::vector<std::string> GetDatabaseNames(const Schema& Options) const;
				bool IsConnected() const;
			};

			class TH_OUT Cluster : public Core::Object
			{
			private:
				std::atomic<bool> Connected;
				TConnectionPool* Pool;
				Address SrcAddress;

			public:
				Cluster();
				virtual ~Cluster() override;
				Core::Async<bool> Connect(const std::string& Address);
				Core::Async<bool> Connect(Address* URI);
				Core::Async<bool> Disconnect();
				void SetProfile(const char* Name);
				void Push(Connection** Client);
				Connection* Pop();
				TConnectionPool* Get() const;
				Address GetAddress() const;
			};

			class TH_OUT Driver
			{
			private:
				struct Pose
				{
					std::string Key;
					size_t Offset = 0;
					bool Escape = false;
				};

				struct Sequence
				{
					std::vector<Pose> Positions;
					std::string Request;
					Schema Cache;

					Sequence();
				};

			private:
				static std::unordered_map<std::string, Sequence>* Queries;
				static std::mutex* Safe;
				static std::atomic<int> State;
				static OnQueryLog Logger;
				static void* APM;

			public:
				static void Create();
				static void Release();
				static void SetQueryLog(const OnQueryLog& Callback);
				static void AttachQueryLog(TConnection* Connection);
				static void AttachQueryLog(TConnectionPool* Connection);
				static bool AddQuery(const std::string& Name, const char* Buffer, size_t Size);
				static bool AddDirectory(const std::string& Directory, const std::string& Origin = "");
				static bool RemoveQuery(const std::string& Name);
				static Schema GetQuery(const std::string& Name, Core::SchemaArgs* Map, bool Once = true);
				static std::vector<std::string> GetQueries();

			private:
				static std::string GetJSON(Core::Schema* Source, bool Escape);
			};
		}
	}
}
#endif
