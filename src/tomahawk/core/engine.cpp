#include "engine.h"
#include "../engine/components.h"
#include "../engine/processors.h"
#include "../engine/renderers.h"
#include "../network/http.h"
#include "../audio/effects.h"
#include "../audio/filters.h"
#include <sstream>
#ifdef TH_HAS_SDL2
#include <SDL2/SDL_syswm.h>
#undef Complex
#endif

namespace Tomahawk
{
	namespace Engine
	{
		Event::Event(const std::string& NewName) noexcept : Name(NewName)
		{
		}
		Event::Event(const std::string& NewName, const Core::VariantArgs& NewArgs) noexcept : Name(NewName), Args(NewArgs)
		{
		}
		Event::Event(const std::string& NewName, Core::VariantArgs&& NewArgs) noexcept : Name(NewName), Args(std::move(NewArgs))
		{
		}
		Event::Event(const Event& Other) noexcept : Name(Other.Name), Args(Other.Args)
		{
		}
		Event::Event(Event&& Other) noexcept : Name(std::move(Other.Name)), Args(std::move(Other.Args))
		{
		}
		Event& Event::operator= (const Event& Other) noexcept
		{
			Name = Other.Name;
			Args = Other.Args;
			return *this;
		}
		Event& Event::operator= (Event&& Other) noexcept
		{
			Name = std::move(Other.Name);
			Args = std::move(Other.Args);
			return *this;
		}

		AssetFile::AssetFile(char* SrcBuffer, size_t SrcSize) : Buffer(SrcBuffer), Size(SrcSize)
		{
		}
		AssetFile::~AssetFile()
		{
			if (Buffer != nullptr)
				TH_FREE(Buffer);
		}
		char* AssetFile::GetBuffer()
		{
			return Buffer;
		}
		size_t AssetFile::GetSize()
		{
			return Size;
		}

		FragmentQuery::FragmentQuery() : Fragments(1), Query(nullptr), Satisfied(1)
		{

		}
		FragmentQuery::~FragmentQuery()
		{
			TH_RELEASE(Query);
		}
		bool FragmentQuery::Begin(Graphics::GraphicsDevice* Device)
		{
			TH_ASSERT(Device != nullptr, false, "graphics device should be set");
			if (!Query)
			{
				Graphics::Query::Desc I;
				I.Predicate = false;

				Query = Device->CreateQuery(I);
			}

			if (Satisfied == 1)
			{
				Satisfied = 0;
				Device->QueryBegin(Query);
				return true;
			}

			if (Satisfied > 1)
				Satisfied--;

			return Fragments > 0;
		}
		void FragmentQuery::End(Graphics::GraphicsDevice* Device)
		{
			TH_ASSERT_V(Device != nullptr, "graphics device should be set");
			if (Satisfied == 0)
			{
				Satisfied = -1;
				Device->QueryEnd(Query);
			}
		}
		void FragmentQuery::Clear()
		{
			Fragments = 1;
		}
		int FragmentQuery::Fetch(RenderSystem* System)
		{
			TH_ASSERT(System != nullptr, -1, "render system should be set");
			if (!Query || Satisfied != -1)
				return -1;

			if (!System->GetDevice()->GetQueryData(Query, &Fragments))
				return -1;

			Satisfied = 1 + System->StallFrames;
			return Fragments > 0;
		}
		uint64_t FragmentQuery::GetPassed()
		{
			return Fragments;
		}

		void Viewer::Set(const Compute::Matrix4x4& _View, const Compute::Matrix4x4& _Projection, const Compute::Vector3& _Position, float _Near, float _Far)
		{
			Set(_View, _Projection, _Position, -_View.Rotation(), _Near, _Far);
		}
		void Viewer::Set(const Compute::Matrix4x4& _View, const Compute::Matrix4x4& _Projection, const Compute::Vector3& _Position, const Compute::Vector3& _Rotation, float _Near, float _Far)
		{
			View = _View;
			Projection = _Projection;
			ViewProjection = _View * _Projection;
			InvViewProjection = ViewProjection.Inv();
			InvPosition = _Position.Inv();
			Position = _Position;
			Rotation = _Rotation;
			FarPlane = (_Far < _Near ? 999999999 : _Far);
			NearPlane = _Near;
			CubicViewProjection[0] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::PositiveX, Position) * Projection;
			CubicViewProjection[1] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::NegativeX, Position) * Projection;
			CubicViewProjection[2] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::PositiveY, Position) * Projection;
			CubicViewProjection[3] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::NegativeY, Position) * Projection;
			CubicViewProjection[4] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::PositiveZ, Position) * Projection;
			CubicViewProjection[5] = Compute::Matrix4x4::CreateLookAt(Compute::CubeFace::NegativeZ, Position) * Projection;
		}

		void NMake::Pack(Core::Document* V, bool Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("b", Core::Var::Boolean(Value));
		}
		void NMake::Pack(Core::Document* V, int Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("i", Core::Var::Integer(Value));
		}
		void NMake::Pack(Core::Document* V, unsigned int Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("i", Core::Var::Integer(Value));
		}
		void NMake::Pack(Core::Document* V, unsigned long Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("i", Core::Var::Integer(Value));
		}
		void NMake::Pack(Core::Document* V, float Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("n", Core::Var::Number(Value));
		}
		void NMake::Pack(Core::Document* V, double Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("n", Core::Var::Number(Value));
		}
		void NMake::Pack(Core::Document* V, int64_t Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("i", Core::Var::Integer(Value));
		}
		void NMake::Pack(Core::Document* V, long double Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("n", Core::Var::Number(Value));
		}
		void NMake::Pack(Core::Document* V, unsigned long long Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("i", Core::Var::Integer(Value));
		}
		void NMake::Pack(Core::Document* V, const char* Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("s", Core::Var::String(Value ? Value : ""));
		}
		void NMake::Pack(Core::Document* V, const Compute::Vector2& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("x", Core::Var::Number(Value.X));
			V->SetAttribute("y", Core::Var::Number(Value.Y));
		}
		void NMake::Pack(Core::Document* V, const Compute::Vector3& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("x", Core::Var::Number(Value.X));
			V->SetAttribute("y", Core::Var::Number(Value.Y));
			V->SetAttribute("z", Core::Var::Number(Value.Z));
		}
		void NMake::Pack(Core::Document* V, const Compute::Vector4& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("x", Core::Var::Number(Value.X));
			V->SetAttribute("y", Core::Var::Number(Value.Y));
			V->SetAttribute("z", Core::Var::Number(Value.Z));
			V->SetAttribute("w", Core::Var::Number(Value.W));
		}
		void NMake::Pack(Core::Document* V, const Compute::Matrix4x4& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("m11", Core::Var::Number(Value.Row[0]));
			V->SetAttribute("m12", Core::Var::Number(Value.Row[1]));
			V->SetAttribute("m13", Core::Var::Number(Value.Row[2]));
			V->SetAttribute("m14", Core::Var::Number(Value.Row[3]));
			V->SetAttribute("m21", Core::Var::Number(Value.Row[4]));
			V->SetAttribute("m22", Core::Var::Number(Value.Row[5]));
			V->SetAttribute("m23", Core::Var::Number(Value.Row[6]));
			V->SetAttribute("m24", Core::Var::Number(Value.Row[7]));
			V->SetAttribute("m31", Core::Var::Number(Value.Row[8]));
			V->SetAttribute("m32", Core::Var::Number(Value.Row[9]));
			V->SetAttribute("m33", Core::Var::Number(Value.Row[10]));
			V->SetAttribute("m34", Core::Var::Number(Value.Row[11]));
			V->SetAttribute("m41", Core::Var::Number(Value.Row[12]));
			V->SetAttribute("m42", Core::Var::Number(Value.Row[13]));
			V->SetAttribute("m43", Core::Var::Number(Value.Row[14]));
			V->SetAttribute("m44", Core::Var::Number(Value.Row[15]));
		}
		void NMake::Pack(Core::Document* V, const Attenuation& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("range"), Value.Range);
			NMake::Pack(V->Set("c1"), Value.C1);
			NMake::Pack(V->Set("c2"), Value.C2);
		}
		void NMake::Pack(Core::Document* V, const AnimatorState& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("looped"), Value.Looped);
			NMake::Pack(V->Set("paused"), Value.Paused);
			NMake::Pack(V->Set("blended"), Value.Blended);
			NMake::Pack(V->Set("clip"), Value.Clip);
			NMake::Pack(V->Set("frame"), Value.Frame);
			NMake::Pack(V->Set("rate"), Value.Rate);
			NMake::Pack(V->Set("duration"), Value.Duration);
			NMake::Pack(V->Set("time"), Value.Time);
		}
		void NMake::Pack(Core::Document* V, const SpawnerProperties& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("iterations"), Value.Iterations);

			Core::Document* Angular = V->Set("angular");
			NMake::Pack(Angular->Set("intensity"), Value.Angular.Intensity);
			NMake::Pack(Angular->Set("accuracy"), Value.Angular.Accuracy);
			NMake::Pack(Angular->Set("min"), Value.Angular.Min);
			NMake::Pack(Angular->Set("max"), Value.Angular.Max);

			Core::Document* Diffusion = V->Set("diffusion");
			NMake::Pack(Diffusion->Set("intensity"), Value.Diffusion.Intensity);
			NMake::Pack(Diffusion->Set("accuracy"), Value.Diffusion.Accuracy);
			NMake::Pack(Diffusion->Set("min"), Value.Diffusion.Min);
			NMake::Pack(Diffusion->Set("max"), Value.Diffusion.Max);

			Core::Document* Noise = V->Set("noise");
			NMake::Pack(Noise->Set("intensity"), Value.Noise.Intensity);
			NMake::Pack(Noise->Set("accuracy"), Value.Noise.Accuracy);
			NMake::Pack(Noise->Set("min"), Value.Noise.Min);
			NMake::Pack(Noise->Set("max"), Value.Noise.Max);

			Core::Document* Position = V->Set("position");
			NMake::Pack(Position->Set("intensity"), Value.Position.Intensity);
			NMake::Pack(Position->Set("accuracy"), Value.Position.Accuracy);
			NMake::Pack(Position->Set("min"), Value.Position.Min);
			NMake::Pack(Position->Set("max"), Value.Position.Max);

			Core::Document* Rotation = V->Set("rotation");
			NMake::Pack(Rotation->Set("intensity"), Value.Rotation.Intensity);
			NMake::Pack(Rotation->Set("accuracy"), Value.Rotation.Accuracy);
			NMake::Pack(Rotation->Set("min"), Value.Rotation.Min);
			NMake::Pack(Rotation->Set("max"), Value.Rotation.Max);

			Core::Document* Scale = V->Set("scale");
			NMake::Pack(Scale->Set("intensity"), Value.Scale.Intensity);
			NMake::Pack(Scale->Set("accuracy"), Value.Scale.Accuracy);
			NMake::Pack(Scale->Set("min"), Value.Scale.Min);
			NMake::Pack(Scale->Set("max"), Value.Scale.Max);

			Core::Document* Velocity = V->Set("velocity");
			NMake::Pack(Velocity->Set("intensity"), Value.Velocity.Intensity);
			NMake::Pack(Velocity->Set("accuracy"), Value.Velocity.Accuracy);
			NMake::Pack(Velocity->Set("min"), Value.Velocity.Min);
			NMake::Pack(Velocity->Set("max"), Value.Velocity.Max);
		}
		void NMake::Pack(Core::Document* V, Material* Value, ContentManager* Content)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			TH_ASSERT_V(Content != nullptr, "content manager should be set");
			TH_ASSERT_V(Value != nullptr, "value should be set");

			AssetCache* Asset = Content->Find<Graphics::Texture2D>(Value->GetDiffuseMap());
			if (Asset)
				NMake::Pack(V->Set("diffuse-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetNormalMap());
			if (Asset)
				NMake::Pack(V->Set("normal-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetMetallicMap());
			if (Asset)
				NMake::Pack(V->Set("metallic-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetRoughnessMap());
			if (Asset)
				NMake::Pack(V->Set("roughness-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetHeightMap());
			if (Asset)
				NMake::Pack(V->Set("height-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetOcclusionMap());
			if (Asset)
				NMake::Pack(V->Set("occlusion-map"), Asset->Path);

			Asset = Content->Find<Graphics::Texture2D>(Value->GetEmissionMap());
			if (Asset)
				NMake::Pack(V->Set("emission-map"), Asset->Path);

			NMake::Pack(V->Set("emission"), Value->Surface.Emission);
			NMake::Pack(V->Set("metallic"), Value->Surface.Metallic);
			NMake::Pack(V->Set("diffuse"), Value->Surface.Diffuse);
			NMake::Pack(V->Set("scatter"), Value->Surface.Scatter);
			NMake::Pack(V->Set("roughness"), Value->Surface.Roughness);
			NMake::Pack(V->Set("occlusion"), Value->Surface.Occlusion);
			NMake::Pack(V->Set("fresnel"), Value->Surface.Fresnel);
			NMake::Pack(V->Set("refraction"), Value->Surface.Refraction);
			NMake::Pack(V->Set("transparency"), Value->Surface.Transparency);
			NMake::Pack(V->Set("environment"), Value->Surface.Environment);
			NMake::Pack(V->Set("radius"), Value->Surface.Radius);
			NMake::Pack(V->Set("height"), Value->Surface.Height);
			NMake::Pack(V->Set("bias"), Value->Surface.Bias);
			NMake::Pack(V->Set("name"), Value->GetName());
		}
		void NMake::Pack(Core::Document* V, const Compute::SkinAnimatorKey& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("pose"), Value.Pose);
			NMake::Pack(V->Set("time"), Value.Time);
		}
		void NMake::Pack(Core::Document* V, const Compute::SkinAnimatorClip& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("name"), Value.Name);
			NMake::Pack(V->Set("duration"), Value.Duration);
			NMake::Pack(V->Set("rate"), Value.Rate);

			Core::Document* Array = V->Set("frames", Core::Var::Array());
			for (auto&& It : Value.Keys)
				NMake::Pack(Array->Set("frame"), It);
		}
		void NMake::Pack(Core::Document* V, const Compute::KeyAnimatorClip& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("name"), Value.Name);
			NMake::Pack(V->Set("rate"), Value.Rate);
			NMake::Pack(V->Set("duration"), Value.Duration);
			NMake::Pack(V->Set("frames"), Value.Keys);
		}
		void NMake::Pack(Core::Document* V, const Compute::AnimatorKey& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("position"), Value.Position);
			NMake::Pack(V->Set("rotation"), Value.Rotation);
			NMake::Pack(V->Set("scale"), Value.Scale);
			NMake::Pack(V->Set("time"), Value.Time);
		}
		void NMake::Pack(Core::Document* V, const Compute::ElementVertex& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("px", Core::Var::Number(Value.PositionX));
			V->SetAttribute("py", Core::Var::Number(Value.PositionY));
			V->SetAttribute("pz", Core::Var::Number(Value.PositionZ));
			V->SetAttribute("vx", Core::Var::Number(Value.VelocityX));
			V->SetAttribute("vy", Core::Var::Number(Value.VelocityY));
			V->SetAttribute("vz", Core::Var::Number(Value.VelocityZ));
			V->SetAttribute("cx", Core::Var::Number(Value.ColorX));
			V->SetAttribute("cy", Core::Var::Number(Value.ColorY));
			V->SetAttribute("cz", Core::Var::Number(Value.ColorZ));
			V->SetAttribute("cw", Core::Var::Number(Value.ColorW));
			V->SetAttribute("a", Core::Var::Number(Value.Angular));
			V->SetAttribute("s", Core::Var::Number(Value.Scale));
			V->SetAttribute("r", Core::Var::Number(Value.Rotation));
		}
		void NMake::Pack(Core::Document* V, const Compute::Vertex& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("px", Core::Var::Number(Value.PositionX));
			V->SetAttribute("py", Core::Var::Number(Value.PositionY));
			V->SetAttribute("pz", Core::Var::Number(Value.PositionZ));
			V->SetAttribute("tx", Core::Var::Number(Value.TexCoordX));
			V->SetAttribute("ty", Core::Var::Number(Value.TexCoordY));
			V->SetAttribute("nx", Core::Var::Number(Value.NormalX));
			V->SetAttribute("ny", Core::Var::Number(Value.NormalY));
			V->SetAttribute("nz", Core::Var::Number(Value.NormalZ));
			V->SetAttribute("tnx", Core::Var::Number(Value.TangentX));
			V->SetAttribute("tny", Core::Var::Number(Value.TangentY));
			V->SetAttribute("tnz", Core::Var::Number(Value.TangentZ));
			V->SetAttribute("btx", Core::Var::Number(Value.BitangentX));
			V->SetAttribute("bty", Core::Var::Number(Value.BitangentY));
			V->SetAttribute("btz", Core::Var::Number(Value.BitangentZ));
		}
		void NMake::Pack(Core::Document* V, const Compute::SkinVertex& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("px", Core::Var::Number(Value.PositionX));
			V->SetAttribute("py", Core::Var::Number(Value.PositionY));
			V->SetAttribute("pz", Core::Var::Number(Value.PositionZ));
			V->SetAttribute("tx", Core::Var::Number(Value.TexCoordX));
			V->SetAttribute("ty", Core::Var::Number(Value.TexCoordY));
			V->SetAttribute("nx", Core::Var::Number(Value.NormalX));
			V->SetAttribute("ny", Core::Var::Number(Value.NormalY));
			V->SetAttribute("nz", Core::Var::Number(Value.NormalZ));
			V->SetAttribute("tnx", Core::Var::Number(Value.TangentX));
			V->SetAttribute("tny", Core::Var::Number(Value.TangentY));
			V->SetAttribute("tnz", Core::Var::Number(Value.TangentZ));
			V->SetAttribute("btx", Core::Var::Number(Value.BitangentX));
			V->SetAttribute("bty", Core::Var::Number(Value.BitangentY));
			V->SetAttribute("btz", Core::Var::Number(Value.BitangentZ));
			V->SetAttribute("ji0", Core::Var::Number(Value.JointIndex0));
			V->SetAttribute("ji1", Core::Var::Number(Value.JointIndex1));
			V->SetAttribute("ji2", Core::Var::Number(Value.JointIndex2));
			V->SetAttribute("ji3", Core::Var::Number(Value.JointIndex3));
			V->SetAttribute("jb0", Core::Var::Number(Value.JointBias0));
			V->SetAttribute("jb1", Core::Var::Number(Value.JointBias1));
			V->SetAttribute("jb2", Core::Var::Number(Value.JointBias2));
			V->SetAttribute("jb3", Core::Var::Number(Value.JointBias3));
		}
		void NMake::Pack(Core::Document* V, const Compute::Joint& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			NMake::Pack(V->Set("index"), Value.Index);
			NMake::Pack(V->Set("name"), Value.Name);
			NMake::Pack(V->Set("transform"), Value.Transform);
			NMake::Pack(V->Set("bind-shape"), Value.BindShape);

			Core::Document* Joints = V->Set("childs", Core::Var::Array());
			for (auto& It : Value.Childs)
				NMake::Pack(Joints->Set("joint"), It);
		}
		void NMake::Pack(Core::Document* V, const Core::Ticker& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("delay", Core::Var::Number(Value.Delay));
		}
		void NMake::Pack(Core::Document* V, const std::string& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			V->SetAttribute("s", Core::Var::String(Value));
		}
		void NMake::Pack(Core::Document* V, const std::vector<bool>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("b-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<int>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("i-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<unsigned int>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("i-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<float>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("n-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<double>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("n-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<int64_t>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("i-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<long double>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("n-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<uint64_t>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It << " ";

			V->Set("i-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Vector2>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It.X << " " << It.Y << " ";

			V->Set("v2-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Vector3>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It.X << " " << It.Y << " " << It.Z << " ";

			V->Set("v3-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Vector4>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It.X << " " << It.Y << " " << It.Z << " " << It.W << " ";

			V->Set("v4-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Matrix4x4>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				for (float i : It.Row)
					Stream << i << " ";
			}

			V->Set("m4x4-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<AnimatorState>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.Paused << " ";
				Stream << It.Looped << " ";
				Stream << It.Blended << " ";
				Stream << It.Duration << " ";
				Stream << It.Rate << " ";
				Stream << It.Time << " ";
				Stream << It.Frame << " ";
				Stream << It.Clip << " ";
			}

			V->Set("as-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<SpawnerProperties>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.Angular.Accuracy << " " << It.Angular.Min << " " << It.Angular.Max << " ";
				Stream << It.Rotation.Accuracy << " " << It.Rotation.Min << " " << It.Rotation.Max << " ";
				Stream << It.Scale.Accuracy << " " << It.Scale.Min << " " << It.Scale.Max << " ";
				Stream << It.Diffusion.Accuracy << " ";
				Stream << It.Diffusion.Min.X << " " << It.Diffusion.Min.Y << " " << It.Diffusion.Min.Z << " " << It.Diffusion.Min.W << " ";
				Stream << It.Diffusion.Max.X << " " << It.Diffusion.Max.Y << " " << It.Diffusion.Max.Z << " " << It.Diffusion.Max.W << " ";
				Stream << It.Noise.Accuracy << " ";
				Stream << It.Noise.Min.X << " " << It.Noise.Min.Y << " " << It.Noise.Min.Z << " ";
				Stream << It.Noise.Max.X << " " << It.Noise.Max.Y << " " << It.Noise.Max.Z << " ";
				Stream << It.Position.Accuracy << " ";
				Stream << It.Position.Min.X << " " << It.Position.Min.Y << " " << It.Position.Min.Z << " ";
				Stream << It.Position.Max.X << " " << It.Position.Max.Y << " " << It.Position.Max.Z << " ";
				Stream << It.Velocity.Accuracy << " ";
				Stream << It.Velocity.Min.X << " " << It.Velocity.Min.Y << " " << It.Velocity.Min.Z << " ";
				Stream << It.Velocity.Max.X << " " << It.Velocity.Max.Y << " " << It.Velocity.Max.Z << " ";
				Stream << It.Iterations << " ";
			}

			V->Set("sp-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::SkinAnimatorClip>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			Core::Document* Array = V->Set("clips", Core::Var::Array());
			for (auto&& It : Value)
				NMake::Pack(Array->Set("clip"), It);
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::KeyAnimatorClip>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			Core::Document* Array = V->Set("clips", Core::Var::Array());
			for (auto&& It : Value)
				NMake::Pack(Array->Set("clip"), It);
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::AnimatorKey>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.Position.X << " ";
				Stream << It.Position.Y << " ";
				Stream << It.Position.Z << " ";
				Stream << It.Rotation.X << " ";
				Stream << It.Rotation.Y << " ";
				Stream << It.Rotation.Z << " ";
				Stream << It.Scale.X << " ";
				Stream << It.Scale.Y << " ";
				Stream << It.Scale.Z << " ";
				Stream << It.Time << " ";
			}

			V->Set("ak-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::ElementVertex>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.PositionX << " ";
				Stream << It.PositionY << " ";
				Stream << It.PositionZ << " ";
				Stream << It.ColorX << " ";
				Stream << It.ColorY << " ";
				Stream << It.ColorZ << " ";
				Stream << It.ColorW << " ";
				Stream << It.VelocityX << " ";
				Stream << It.VelocityY << " ";
				Stream << It.VelocityZ << " ";
				Stream << It.Angular << " ";
				Stream << It.Rotation << " ";
				Stream << It.Scale << " ";
			}

			V->Set("ev-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Joint>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			for (auto&& It : Value)
				NMake::Pack(V->Set("joint"), It);
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::Vertex>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.PositionX << " ";
				Stream << It.PositionY << " ";
				Stream << It.PositionZ << " ";
				Stream << It.TexCoordX << " ";
				Stream << It.TexCoordY << " ";
				Stream << It.NormalX << " ";
				Stream << It.NormalY << " ";
				Stream << It.NormalZ << " ";
				Stream << It.TangentX << " ";
				Stream << It.TangentY << " ";
				Stream << It.TangentZ << " ";
				Stream << It.BitangentX << " ";
				Stream << It.BitangentY << " ";
				Stream << It.BitangentZ << " ";
				Stream << "-1 -1 -1 -1 0 0 0 0 ";
			}

			V->Set("iv-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Compute::SkinVertex>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
			{
				Stream << It.PositionX << " ";
				Stream << It.PositionY << " ";
				Stream << It.PositionZ << " ";
				Stream << It.TexCoordX << " ";
				Stream << It.TexCoordY << " ";
				Stream << It.NormalX << " ";
				Stream << It.NormalY << " ";
				Stream << It.NormalZ << " ";
				Stream << It.TangentX << " ";
				Stream << It.TangentY << " ";
				Stream << It.TangentZ << " ";
				Stream << It.BitangentX << " ";
				Stream << It.BitangentY << " ";
				Stream << It.BitangentZ << " ";
				Stream << It.JointIndex0 << " ";
				Stream << It.JointIndex1 << " ";
				Stream << It.JointIndex2 << " ";
				Stream << It.JointIndex3 << " ";
				Stream << It.JointBias0 << " ";
				Stream << It.JointBias1 << " ";
				Stream << It.JointBias2 << " ";
				Stream << It.JointBias3 << " ";
			}

			V->Set("iv-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<Core::Ticker>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			std::stringstream Stream;
			for (auto&& It : Value)
				Stream << It.Delay << " ";

			V->Set("tt-array", Core::Var::String(Stream.str().substr(0, Stream.str().size() - 1)));
			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		void NMake::Pack(Core::Document* V, const std::vector<std::string>& Value)
		{
			TH_ASSERT_V(V != nullptr, "document should be set");
			Core::Document* Array = V->Set("s-array", Core::Var::Array());
			for (auto&& It : Value)
				Array->Set("s", Core::Var::String(It));

			V->Set("size", Core::Var::Integer((int64_t)Value.size()));
		}
		bool NMake::Unpack(Core::Document* V, bool* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = V->GetVar("[b]").GetBoolean();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, int* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (int)V->GetVar("[i]").GetInteger();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, unsigned int* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (unsigned int)V->GetVar("[i]").GetInteger();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, unsigned long* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (unsigned long)V->GetVar("[i]").GetInteger();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, float* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (float)V->GetVar("[n]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, double* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (int)V->GetVar("[n]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, long double* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = V->GetVar("[n]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, int64_t* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = V->GetVar("[i]").GetInteger();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, unsigned long long* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = (unsigned long long)V->GetVar("[i]").GetInteger();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Vector2* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->X = (float)V->GetVar("[x]").GetNumber();
			O->Y = (float)V->GetVar("[y]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Vector3* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->X = (float)V->GetVar("[x]").GetNumber();
			O->Y = (float)V->GetVar("[y]").GetNumber();
			O->Z = (float)V->GetVar("[z]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Vector4* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->X = (float)V->GetVar("[x]").GetNumber();
			O->Y = (float)V->GetVar("[y]").GetNumber();
			O->Z = (float)V->GetVar("[z]").GetNumber();
			O->W = (float)V->GetVar("[w]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Matrix4x4* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->Row[0] = (float)V->GetVar("[m11]").GetNumber();
			O->Row[1] = (float)V->GetVar("[m12]").GetNumber();
			O->Row[2] = (float)V->GetVar("[m13]").GetNumber();
			O->Row[3] = (float)V->GetVar("[m14]").GetNumber();
			O->Row[4] = (float)V->GetVar("[m21]").GetNumber();
			O->Row[5] = (float)V->GetVar("[m22]").GetNumber();
			O->Row[6] = (float)V->GetVar("[m23]").GetNumber();
			O->Row[7] = (float)V->GetVar("[m24]").GetNumber();
			O->Row[8] = (float)V->GetVar("[m31]").GetNumber();
			O->Row[9] = (float)V->GetVar("[m32]").GetNumber();
			O->Row[10] = (float)V->GetVar("[m33]").GetNumber();
			O->Row[11] = (float)V->GetVar("[m34]").GetNumber();
			O->Row[12] = (float)V->GetVar("[m41]").GetNumber();
			O->Row[13] = (float)V->GetVar("[m42]").GetNumber();
			O->Row[14] = (float)V->GetVar("[m43]").GetNumber();
			O->Row[15] = (float)V->GetVar("[m44]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Attenuation* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("range"), &O->Range);
			NMake::Unpack(V->Get("c1"), &O->C1);
			NMake::Unpack(V->Get("c2"), &O->C2);
			return true;
		}
		bool NMake::Unpack(Core::Document* V, AnimatorState* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("looped"), &O->Looped);
			NMake::Unpack(V->Get("paused"), &O->Paused);
			NMake::Unpack(V->Get("blended"), &O->Blended);
			NMake::Unpack(V->Get("clip"), &O->Clip);
			NMake::Unpack(V->Get("frame"), &O->Frame);
			NMake::Unpack(V->Get("rate"), &O->Rate);
			NMake::Unpack(V->Get("duration"), &O->Duration);
			NMake::Unpack(V->Get("time"), &O->Time);
			return true;
		}
		bool NMake::Unpack(Core::Document* V, SpawnerProperties* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("iterations"), &O->Iterations);

			Core::Document* Angular = V->Get("angular");
			NMake::Unpack(Angular->Get("intensity"), &O->Angular.Intensity);
			NMake::Unpack(Angular->Get("accuracy"), &O->Angular.Accuracy);
			NMake::Unpack(Angular->Get("min"), &O->Angular.Min);
			NMake::Unpack(Angular->Get("max"), &O->Angular.Max);

			Core::Document* Diffusion = V->Get("diffusion");
			NMake::Unpack(Diffusion->Get("intensity"), &O->Diffusion.Intensity);
			NMake::Unpack(Diffusion->Get("accuracy"), &O->Diffusion.Accuracy);
			NMake::Unpack(Diffusion->Get("min"), &O->Diffusion.Min);
			NMake::Unpack(Diffusion->Get("max"), &O->Diffusion.Max);

			Core::Document* Noise = V->Get("noise");
			NMake::Unpack(Noise->Get("intensity"), &O->Noise.Intensity);
			NMake::Unpack(Noise->Get("accuracy"), &O->Noise.Accuracy);
			NMake::Unpack(Noise->Get("min"), &O->Noise.Min);
			NMake::Unpack(Noise->Get("max"), &O->Noise.Max);

			Core::Document* Position = V->Get("position");
			NMake::Unpack(Position->Get("intensity"), &O->Position.Intensity);
			NMake::Unpack(Position->Get("accuracy"), &O->Position.Accuracy);
			NMake::Unpack(Position->Get("min"), &O->Position.Min);
			NMake::Unpack(Position->Get("max"), &O->Position.Max);

			Core::Document* Rotation = V->Get("rotation");
			NMake::Unpack(Rotation->Get("intensity"), &O->Rotation.Intensity);
			NMake::Unpack(Rotation->Get("accuracy"), &O->Rotation.Accuracy);
			NMake::Unpack(Rotation->Get("min"), &O->Rotation.Min);
			NMake::Unpack(Rotation->Get("max"), &O->Rotation.Max);

			Core::Document* Scale = V->Get("scale");
			NMake::Unpack(Scale->Get("intensity"), &O->Scale.Intensity);
			NMake::Unpack(Scale->Get("accuracy"), &O->Scale.Accuracy);
			NMake::Unpack(Scale->Get("min"), &O->Scale.Min);
			NMake::Unpack(Scale->Get("max"), &O->Scale.Max);

			Core::Document* Velocity = V->Get("velocity");
			NMake::Unpack(Velocity->Get("intensity"), &O->Velocity.Intensity);
			NMake::Unpack(Velocity->Get("accuracy"), &O->Velocity.Accuracy);
			NMake::Unpack(Velocity->Get("min"), &O->Velocity.Min);
			NMake::Unpack(Velocity->Get("max"), &O->Velocity.Max);

			return true;
		}
		bool NMake::Unpack(Core::Document* V, Material* O, ContentManager* Content)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			TH_ASSERT(Content != nullptr, false, "content manager should be set");
			if (!V)
				return false;

			std::string Path;
			if (NMake::Unpack(V->Get("diffuse-map"), &Path))
				O->SetDiffuseMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("normal-map"), &Path))
				O->SetNormalMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("metallic-map"), &Path))
				O->SetMetallicMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("roughness-map"), &Path))
				O->SetRoughnessMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("height-map"), &Path))
				O->SetHeightMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("occlusion-map"), &Path))
				O->SetOcclusionMap(Content->Load<Graphics::Texture2D>(Path));

			if (NMake::Unpack(V->Get("emission-map"), &Path))
				O->SetEmissionMap(Content->Load<Graphics::Texture2D>(Path));

			Path.clear();
			NMake::Unpack(V->Get("emission"), &O->Surface.Emission);
			NMake::Unpack(V->Get("metallic"), &O->Surface.Metallic);
			NMake::Unpack(V->Get("diffuse"), &O->Surface.Diffuse);
			NMake::Unpack(V->Get("scatter"), &O->Surface.Scatter);
			NMake::Unpack(V->Get("roughness"), &O->Surface.Roughness);
			NMake::Unpack(V->Get("occlusion"), &O->Surface.Occlusion);
			NMake::Unpack(V->Get("fresnel"), &O->Surface.Fresnel);
			NMake::Unpack(V->Get("refraction"), &O->Surface.Refraction);
			NMake::Unpack(V->Get("transparency"), &O->Surface.Transparency);
			NMake::Unpack(V->Get("environment"), &O->Surface.Environment);
			NMake::Unpack(V->Get("radius"), &O->Surface.Radius);
			NMake::Unpack(V->Get("height"), &O->Surface.Height);
			NMake::Unpack(V->Get("bias"), &O->Surface.Bias);
			NMake::Unpack(V->Get("name"), &Path);
			O->SetName(Path, true);

			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::SkinAnimatorKey* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("pose"), &O->Pose);
			NMake::Unpack(V->Get("time"), &O->Time);

			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::SkinAnimatorClip* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("name"), &O->Name);
			NMake::Unpack(V->Get("duration"), &O->Duration);
			NMake::Unpack(V->Get("rate"), &O->Rate);

			std::vector<Core::Document*> Frames = V->FetchCollection("frames.frame", false);
			for (auto&& It : Frames)
			{
				O->Keys.emplace_back();
				NMake::Unpack(It, &O->Keys.back());
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::KeyAnimatorClip* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("name"), &O->Name);
			NMake::Unpack(V->Get("duration"), &O->Duration);
			NMake::Unpack(V->Get("rate"), &O->Rate);
			NMake::Unpack(V->Get("frames"), &O->Keys);
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::AnimatorKey* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("position"), &O->Position);
			NMake::Unpack(V->Get("rotation"), &O->Rotation);
			NMake::Unpack(V->Get("scale"), &O->Scale);
			NMake::Unpack(V->Get("time"), &O->Time);
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Joint* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			NMake::Unpack(V->Get("index"), &O->Index);
			NMake::Unpack(V->Get("name"), &O->Name);
			NMake::Unpack(V->Get("transform"), &O->Transform);
			NMake::Unpack(V->Get("bind-shape"), &O->BindShape);

			std::vector<Core::Document*> Joints = V->FetchCollection("childs.joint", false);
			for (auto& It : Joints)
			{
				O->Childs.emplace_back();
				NMake::Unpack(It, &O->Childs.back());
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::ElementVertex* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->PositionX = (float)V->GetVar("[px]").GetNumber();
			O->PositionY = (float)V->GetVar("[py]").GetNumber();
			O->PositionZ = (float)V->GetVar("[pz]").GetNumber();
			O->VelocityX = (float)V->GetVar("[vx]").GetNumber();
			O->VelocityY = (float)V->GetVar("[vy]").GetNumber();
			O->VelocityZ = (float)V->GetVar("[vz]").GetNumber();
			O->ColorX = (float)V->GetVar("[cx]").GetNumber();
			O->ColorY = (float)V->GetVar("[cy]").GetNumber();
			O->ColorZ = (float)V->GetVar("[cz]").GetNumber();
			O->ColorW = (float)V->GetVar("[cw]").GetNumber();
			O->Angular = (float)V->GetVar("[a]").GetNumber();
			O->Scale = (float)V->GetVar("[s]").GetNumber();
			O->Rotation = (float)V->GetVar("[r]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::Vertex* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->PositionX = (float)V->GetVar("[px]").GetNumber();
			O->PositionY = (float)V->GetVar("[py]").GetNumber();
			O->PositionZ = (float)V->GetVar("[pz]").GetNumber();
			O->TexCoordX = (float)V->GetVar("[tx]").GetNumber();
			O->TexCoordY = (float)V->GetVar("[ty]").GetNumber();
			O->NormalX = (float)V->GetVar("[nx]").GetNumber();
			O->NormalY = (float)V->GetVar("[ny]").GetNumber();
			O->NormalZ = (float)V->GetVar("[nz]").GetNumber();
			O->TangentX = (float)V->GetVar("[tnx]").GetNumber();
			O->TangentY = (float)V->GetVar("[tny]").GetNumber();
			O->TangentZ = (float)V->GetVar("[tnz]").GetNumber();
			O->BitangentX = (float)V->GetVar("[btx]").GetNumber();
			O->BitangentY = (float)V->GetVar("[bty]").GetNumber();
			O->BitangentZ = (float)V->GetVar("[btz]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Compute::SkinVertex* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->PositionX = (float)V->GetVar("[px]").GetNumber();
			O->PositionY = (float)V->GetVar("[py]").GetNumber();
			O->PositionZ = (float)V->GetVar("[pz]").GetNumber();
			O->TexCoordX = (float)V->GetVar("[tx]").GetNumber();
			O->TexCoordY = (float)V->GetVar("[ty]").GetNumber();
			O->NormalX = (float)V->GetVar("[nx]").GetNumber();
			O->NormalY = (float)V->GetVar("[ny]").GetNumber();
			O->NormalZ = (float)V->GetVar("[nz]").GetNumber();
			O->TangentX = (float)V->GetVar("[tnx]").GetNumber();
			O->TangentY = (float)V->GetVar("[tny]").GetNumber();
			O->TangentZ = (float)V->GetVar("[tnz]").GetNumber();
			O->BitangentX = (float)V->GetVar("[btx]").GetNumber();
			O->BitangentY = (float)V->GetVar("[bty]").GetNumber();
			O->BitangentZ = (float)V->GetVar("[btz]").GetNumber();
			O->JointIndex0 = (float)V->GetVar("[ji0]").GetNumber();
			O->JointIndex1 = (float)V->GetVar("[ji1]").GetNumber();
			O->JointIndex2 = (float)V->GetVar("[ji2]").GetNumber();
			O->JointIndex3 = (float)V->GetVar("[ji3]").GetNumber();
			O->JointBias0 = (float)V->GetVar("[jb0]").GetNumber();
			O->JointBias1 = (float)V->GetVar("[jb1]").GetNumber();
			O->JointBias2 = (float)V->GetVar("[jb2]").GetNumber();
			O->JointBias3 = (float)V->GetVar("[jb3]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, Core::Ticker* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->Delay = (float)V->GetVar("[delay]").GetNumber();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::string* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			*O = V->GetVar("[s]").GetBlob();
			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<bool>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("b-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				bool Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<int>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("i-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				int Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<unsigned int>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("i-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				unsigned int Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<float>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("n-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				float Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<double>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("n-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				double Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<int64_t>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("i-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				int64_t Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<long double>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("n-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				long double Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<uint64_t>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("i-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto It = O->begin(); It != O->end(); ++It)
			{
				uint64_t Item;
				Stream >> Item;
				*It = Item;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Vector2>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("v2-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
				Stream >> It.X >> It.Y;

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Vector3>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("v3-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
				Stream >> It.X >> It.Y >> It.Z;

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Vector4>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("v4-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
				Stream >> It.X >> It.Y >> It.Z >> It.W;

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Matrix4x4>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("m4x4-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				for (int64_t i = 0; i < 16; i++)
					Stream >> It.Row[i];
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<AnimatorState>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("as-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				Stream >> It.Paused;
				Stream >> It.Looped;
				Stream >> It.Blended;
				Stream >> It.Duration;
				Stream >> It.Rate;
				Stream >> It.Time;
				Stream >> It.Frame;
				Stream >> It.Clip;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<SpawnerProperties>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("sp-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				Stream >> It.Angular.Accuracy >> It.Angular.Min >> It.Angular.Max;
				Stream >> It.Rotation.Accuracy >> It.Rotation.Min >> It.Rotation.Max;
				Stream >> It.Scale.Accuracy >> It.Scale.Min >> It.Scale.Max;
				Stream >> It.Diffusion.Accuracy;
				Stream >> It.Diffusion.Min.X >> It.Diffusion.Min.Y >> It.Diffusion.Min.Z >> It.Diffusion.Min.W;
				Stream >> It.Diffusion.Max.X >> It.Diffusion.Max.Y >> It.Diffusion.Max.Z >> It.Diffusion.Max.W;
				Stream >> It.Noise.Accuracy;
				Stream >> It.Noise.Min.X >> It.Noise.Min.Y >> It.Noise.Min.Z;
				Stream >> It.Noise.Max.X >> It.Noise.Max.Y >> It.Noise.Max.Z;
				Stream >> It.Position.Accuracy;
				Stream >> It.Position.Min.X >> It.Position.Min.Y >> It.Position.Min.Z;
				Stream >> It.Position.Max.X >> It.Position.Max.Y >> It.Position.Max.Z;
				Stream >> It.Velocity.Accuracy;
				Stream >> It.Velocity.Min.X >> It.Velocity.Min.Y >> It.Velocity.Min.Z;
				Stream >> It.Velocity.Max.X >> It.Velocity.Max.Y >> It.Velocity.Max.Z;
				Stream >> It.Iterations;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::SkinAnimatorClip>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::vector<Core::Document*> Frames = V->FetchCollection("clips.clip", false);
			for (auto&& It : Frames)
			{
				O->push_back(Compute::SkinAnimatorClip());
				NMake::Unpack(It, &O->back());
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::KeyAnimatorClip>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::vector<Core::Document*> Frames = V->FetchCollection("clips.clip", false);
			for (auto&& It : Frames)
			{
				O->push_back(Compute::KeyAnimatorClip());
				NMake::Unpack(It, &O->back());
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::AnimatorKey>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("ak-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				Stream >> It.Position.X >> It.Position.Y >> It.Position.Z >> It.Rotation.X >> It.Rotation.Y >> It.Rotation.Z;
				Stream >> It.Scale.X >> It.Scale.Y >> It.Scale.Z >> It.Time;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::ElementVertex>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("ev-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				Stream >> It.PositionX;
				Stream >> It.PositionY;
				Stream >> It.PositionZ;
				Stream >> It.ColorX;
				Stream >> It.ColorY;
				Stream >> It.ColorZ;
				Stream >> It.ColorW;
				Stream >> It.VelocityX;
				Stream >> It.VelocityY;
				Stream >> It.VelocityZ;
				Stream >> It.Angular;
				Stream >> It.Rotation;
				Stream >> It.Scale;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Joint>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			O->reserve(V->Size());
			for (auto&& It : V->GetChilds())
			{
				O->push_back(Compute::Joint());
				NMake::Unpack(It, &O->back());
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::Vertex>* O)
		{
			if (!V || !O)
				return false;

			std::string Array(V->GetVar("iv-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			float Dummy;
			for (auto& It : *O)
			{
				Stream >> It.PositionX;
				Stream >> It.PositionY;
				Stream >> It.PositionZ;
				Stream >> It.TexCoordX;
				Stream >> It.TexCoordY;
				Stream >> It.NormalX;
				Stream >> It.NormalY;
				Stream >> It.NormalZ;
				Stream >> It.TangentX;
				Stream >> It.TangentY;
				Stream >> It.TangentZ;
				Stream >> It.BitangentX;
				Stream >> It.BitangentY;
				Stream >> It.BitangentZ;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
				Stream >> Dummy;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Compute::SkinVertex>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("iv-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
			{
				Stream >> It.PositionX;
				Stream >> It.PositionY;
				Stream >> It.PositionZ;
				Stream >> It.TexCoordX;
				Stream >> It.TexCoordY;
				Stream >> It.NormalX;
				Stream >> It.NormalY;
				Stream >> It.NormalZ;
				Stream >> It.TangentX;
				Stream >> It.TangentY;
				Stream >> It.TangentZ;
				Stream >> It.BitangentX;
				Stream >> It.BitangentY;
				Stream >> It.BitangentZ;
				Stream >> It.JointIndex0;
				Stream >> It.JointIndex1;
				Stream >> It.JointIndex2;
				Stream >> It.JointIndex3;
				Stream >> It.JointBias0;
				Stream >> It.JointBias1;
				Stream >> It.JointBias2;
				Stream >> It.JointBias3;
			}

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<Core::Ticker>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			std::string Array(V->GetVar("tt-array").GetBlob());
			int64_t Size = V->GetVar("size").GetInteger();
			if (Array.empty() || !Size)
				return false;

			std::stringstream Stream(Array);
			O->resize((size_t)Size);

			for (auto& It : *O)
				Stream >> It.Delay;

			return true;
		}
		bool NMake::Unpack(Core::Document* V, std::vector<std::string>* O)
		{
			TH_ASSERT(O != nullptr, false, "output should be set");
			if (!V)
				return false;

			Core::Document* Array = V->Get("s-array");
			if (!Array)
				return false;

			for (auto&& It : Array->GetChilds())
			{
				if (It->Key == "s" && It->Value.GetType() == Core::VarType::String)
					O->push_back(It->Value.GetBlob());
			}

			return true;
		}

		Material::Material(SceneGraph* Src) : DiffuseMap(nullptr), NormalMap(nullptr), MetallicMap(nullptr), RoughnessMap(nullptr), HeightMap(nullptr), OcclusionMap(nullptr), EmissionMap(nullptr), Scene(Src), Slot(0)
		{
		}
		Material::Material(const Material& Other) : Material(Other.Scene)
		{
			memcpy(&Surface, &Other.Surface, sizeof(Subsurface));
			if (Other.DiffuseMap != nullptr)
				DiffuseMap = (Graphics::Texture2D*)Other.DiffuseMap->AddRef();

			if (Other.NormalMap != nullptr)
				NormalMap = (Graphics::Texture2D*)Other.NormalMap->AddRef();

			if (Other.MetallicMap != nullptr)
				MetallicMap = (Graphics::Texture2D*)Other.MetallicMap->AddRef();

			if (Other.RoughnessMap != nullptr)
				RoughnessMap = (Graphics::Texture2D*)Other.RoughnessMap->AddRef();

			if (Other.HeightMap != nullptr)
				HeightMap = (Graphics::Texture2D*)Other.HeightMap->AddRef();

			if (Other.OcclusionMap != nullptr)
				OcclusionMap = (Graphics::Texture2D*)Other.OcclusionMap->AddRef();

			if (Other.EmissionMap != nullptr)
				EmissionMap = (Graphics::Texture2D*)Other.EmissionMap->AddRef();
		}
		Material::~Material()
		{
			TH_RELEASE(DiffuseMap);
			TH_RELEASE(NormalMap);
			TH_RELEASE(MetallicMap);
			TH_RELEASE(RoughnessMap);
			TH_RELEASE(HeightMap);
			TH_RELEASE(OcclusionMap);
			TH_RELEASE(EmissionMap);
		}
		void Material::SetName(const std::string& Value, bool Internal)
		{
			if (!Internal)
			{
				TH_ASSERT_V(Scene != nullptr, "scene should be set");
				Scene->Exclusive([this, Value]()
				{
					if (Name == Value)
						return;

					Name = Value;
					Scene->Mutate(this, "set");
				});
			}
			else
				Name = Value;
		}
		const std::string& Material::GetName() const
		{
			return Name;
		}
		void Material::SetDiffuseMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(DiffuseMap);
			DiffuseMap = New;
		}
		Graphics::Texture2D* Material::GetDiffuseMap() const
		{
			return DiffuseMap;
		}
		void Material::SetNormalMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(NormalMap);
			NormalMap = New;
		}
		Graphics::Texture2D* Material::GetNormalMap() const
		{
			return NormalMap;
		}
		void Material::SetMetallicMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(MetallicMap);
			MetallicMap = New;
		}
		Graphics::Texture2D* Material::GetMetallicMap() const
		{
			return MetallicMap;
		}
		void Material::SetRoughnessMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(RoughnessMap);
			RoughnessMap = New;
		}
		Graphics::Texture2D* Material::GetRoughnessMap() const
		{
			return RoughnessMap;
		}
		void Material::SetHeightMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(HeightMap);
			HeightMap = New;
		}
		Graphics::Texture2D* Material::GetHeightMap() const
		{
			return HeightMap;
		}
		void Material::SetOcclusionMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(OcclusionMap);
			OcclusionMap = New;
		}
		Graphics::Texture2D* Material::GetOcclusionMap() const
		{
			return OcclusionMap;
		}
		void Material::SetEmissionMap(Graphics::Texture2D* New)
		{
			TH_RELEASE(EmissionMap);
			EmissionMap = New;
		}
		Graphics::Texture2D* Material::GetEmissionMap() const
		{
			return EmissionMap;
		}
		SceneGraph* Material::GetScene() const
		{
			return Scene;
		}

		Processor::Processor(ContentManager* NewContent) : Content(NewContent)
		{
		}
		Processor::~Processor()
		{
		}
		void Processor::Free(AssetCache* Asset)
		{
		}
		void* Processor::Duplicate(AssetCache* Asset, const Core::VariantArgs& Args)
		{
			return nullptr;
		}
		void* Processor::Deserialize(Core::Stream* Stream, uint64_t Length, uint64_t Offset, const Core::VariantArgs& Args)
		{
			return nullptr;
		}
		bool Processor::Serialize(Core::Stream* Stream, void* Object, const Core::VariantArgs& Args)
		{
			return false;
		}
		ContentManager* Processor::GetContent()
		{
			return Content;
		}

		Component::Component(Entity* Reference, ActorSet Rule) : Parent(Reference), Set((size_t)Rule), Active(true)
		{
			TH_ASSERT_V(Reference != nullptr, "entity should be set");
		}
		Component::~Component()
		{
		}
		void Component::Deserialize(ContentManager* Content, Core::Document* Node)
		{
		}
		void Component::Serialize(ContentManager* Content, Core::Document* Node)
		{
		}
		void Component::Activate(Component* New)
		{
		}
		void Component::Deactivate()
		{
		}
		void Component::Synchronize(Core::Timer* Time)
		{
		}
		void Component::Update(Core::Timer* Time)
		{
		}
		void Component::Message(const std::string& Name, Core::VariantArgs& Args)
		{
		}
		Entity* Component::GetEntity()
		{
			return Parent;
		}
		Compute::Matrix4x4 Component::GetBoundingBox()
		{
			return Parent->GetTransform()->GetBias();
		}
		void Component::SetActive(bool Enabled)
		{
			if (Active == Enabled)
				return;

			if ((Active = Enabled))
				Activate(nullptr);
			else
				Deactivate();

			auto* Scene = Parent->GetScene();
			auto Components = Scene->GetComponents(GetId());
			if (Active)
			{
				if (Parent->IsActive())
				{
					Components->AddIfNotExists(this);
					if (Set & (size_t)ActorSet::Update)
						Scene->Actors[(size_t)ActorType::Update].AddIfNotExists(this);
					if (Set & (size_t)ActorSet::Synchronize)
						Scene->Actors[(size_t)ActorType::Synchronize].AddIfNotExists(this);
					if (Set & (size_t)ActorSet::Message)
						Scene->Actors[(size_t)ActorType::Message].AddIfNotExists(this);
				}
				else
				{
					Components->Add(this);
					if (Set & (size_t)ActorSet::Update)
						Scene->Actors[(size_t)ActorType::Update].Add(this);
					if (Set & (size_t)ActorSet::Synchronize)
						Scene->Actors[(size_t)ActorType::Synchronize].Add(this);
					if (Set & (size_t)ActorSet::Message)
						Scene->Actors[(size_t)ActorType::Message].Add(this);
				}
			}
			else
			{
				Components->Remove(this);
				if (Set & (size_t)ActorSet::Update)
					Scene->Actors[(size_t)ActorType::Update].Remove(this);
				if (Set & (size_t)ActorSet::Synchronize)
					Scene->Actors[(size_t)ActorType::Synchronize].Remove(this);
				if (Set & (size_t)ActorSet::Message)
					Scene->Actors[(size_t)ActorType::Message].Remove(this);
			}
		}
		bool Component::IsActive()
		{
			return Active;
		}

		Cullable::Cullable(Entity* Ref, ActorSet Rule) : Component(Ref, Rule), Visibility(1.0f)
		{
		}
		void Cullable::ClearCull()
		{
		}
		float Cullable::GetRange()
		{
			const Compute::Vector3& Scale = Parent->GetTransform()->GetScale();
			float Max = Scale.X;

			if (Max < Scale.Y)
				Max = Scale.Y;

			if (Max < Scale.Z)
				return Scale.Z;

			return Max;
		}
		bool Cullable::IsVisible(const Viewer& View, Compute::Matrix4x4* World)
		{
			auto* Transform = Parent->GetTransform();
			if (Transform->GetPosition().Distance(View.Position) > View.FarPlane + Transform->GetScale().Length())
				return false;

			return Compute::Common::IsCubeInFrustum((World ? *World : Transform->GetBias()) * View.ViewProjection, 1.65f);
		}
		bool Cullable::IsNear(const Viewer& View)
		{
			auto* Transform = Parent->GetTransform();
			return Transform->GetPosition().Distance(View.Position) <= View.FarPlane + Transform->GetScale().Length();
		}

		Drawable::Drawable(Entity* Ref, ActorSet Rule, uint64_t Hash, bool vComplex) : Cullable(Ref, Rule | ActorSet::Message), Category(GeoCategory::Opaque), Source(Hash), Complex(vComplex), Static(true)
		{
			if (!Complex)
				Materials[nullptr] = nullptr;
		}
		Drawable::~Drawable()
		{
			Detach();
		}
		void Drawable::Message(const std::string& Name, Core::VariantArgs& Args)
		{
			if (Name != "mutation")
				return;

			Material* Target = (Material*)Args["material"].GetPointer();
			if (!Target || !Args["type"].IsString("push"))
				return;

			for (auto&& Surface : Materials)
			{
				if (Surface.second == Target)
					Surface.second = nullptr;
			}
		}
		void Drawable::ClearCull()
		{
			Query.Clear();
		}
		void Drawable::Attach()
		{
			SetCategory(GeoCategory::Opaque);
		}
		void Drawable::Detach()
		{
			Parent->GetScene()->RemoveDrawable(this, Category);
		}
		bool Drawable::SetCategory(GeoCategory Value)
		{
			Detach();
			Category = Value;
			Parent->GetScene()->AddDrawable(this, Category);
			return true;
		}
		bool Drawable::SetMaterial(void* Instance, Material* Value)
		{
			if (!Complex)
			{
				Materials[nullptr] = Value;
				return (!Instance);
			}

			auto It = Materials.find(Instance);
			if (It == Materials.end())
				Materials[Instance] = Value;
			else
				It->second = Value;

			return true;
		}
		GeoCategory Drawable::GetCategory()
		{
			return Category;
		}
		int64_t Drawable::GetSlot(void* Surface)
		{
			Material* Base = GetMaterial(Surface);
			if (Base != nullptr)
				return (int64_t)Base->Slot;

			return -1;
		}
		int64_t Drawable::GetSlot()
		{
			Material* Base = GetMaterial();
			if (Base != nullptr)
				return (int64_t)Base->Slot;

			return -1;
		}
		Material* Drawable::GetMaterial(void* Instance)
		{
			if (!Complex)
				return nullptr;

			auto It = Materials.find(Instance);
			if (It == Materials.end())
				return nullptr;

			return It->second;
		}
		Material* Drawable::GetMaterial()
		{
			if (Complex || Materials.empty())
				return nullptr;

			return Materials.begin()->second;
		}
		const std::unordered_map<void*, Material*>& Drawable::GetMaterials()
		{
			return Materials;
		}

		Entity::Entity(SceneGraph* Ref) : Scene(Ref), Transform(new Compute::Transform), Active(false), Dirty(true), Tag(-1), Distance(0)
		{
			Transform->UserPointer = this;
			TH_ASSERT_V(Ref != nullptr, "scene should be set");
		}
		Entity::~Entity()
		{
			for (auto& Component : Components)
			{
				if (!Component.second)
					continue;

				Component.second->SetActive(false);
				TH_RELEASE(Component.second);
			}

			TH_RELEASE(Transform);
		}
		void Entity::SetName(const std::string& Value, bool Internal)
		{
			if (!Internal)
			{
				Scene->Exclusive([this, Value]()
				{
					if (Name == Value)
						return;

					Name = Value;
					Scene->Mutate(this, "set");
				});
			}
			else
				Name = Value;
		}
		void Entity::SetRoot(Entity* Parent)
		{
			auto* Old = Transform->GetRoot();
			if (!Parent)
			{
				Transform->SetRoot(nullptr);
				if (Old != nullptr)
					Scene->Mutate(Old->Ptr<Entity>(), this, "pop");
			}
			else
			{
				Transform->SetRoot(Parent->Transform);
				if (Old != Parent->Transform)
					Scene->Mutate(Parent, this, "push");
			}
		}
		void Entity::RemoveComponent(uint64_t fId)
		{
			std::unordered_map<uint64_t, Component*>::iterator It = Components.find(fId);
			if (It == Components.end())
				return;

			It->second->SetActive(false);
			if (Scene->Camera == It->second)
				Scene->Camera = nullptr;

			TH_RELEASE(It->second);
			Components.erase(It);
		}
		void Entity::RemoveChilds()
		{
			Scene->Exclusive([this]()
			{
				std::vector<Compute::Transform*>& Childs = Transform->GetChilds();
				for (size_t i = 0; i < Childs.size(); i++)
				{
					Entity* Entity = Transform->GetChild(i)->Ptr<Engine::Entity>();
					if (!Entity || Entity == this)
						continue;

					Scene->RemoveEntity(Entity, true);
					if (Childs.empty())
						break;

					i--;
				}
			});
		}
		Component* Entity::AddComponent(Component* In)
		{
			TH_ASSERT(In != nullptr, nullptr, "component should be set");
			if (In == GetComponent(In->GetId()))
				return In;

			RemoveComponent(In->GetId());
			In->Active = false;
			In->Parent = this;

			Components.insert({ In->GetId(), In });
			for (auto& Component : Components)
				Component.second->Activate(In == Component.second ? nullptr : In);

			In->SetActive(true);
			return In;
		}
		Component* Entity::GetComponent(uint64_t fId)
		{
			std::unordered_map<uint64_t, Component*>::iterator It = Components.find(fId);
			if (It != Components.end())
				return It->second;

			return nullptr;
		}
		uint64_t Entity::GetComponentCount() const
		{
			return Components.size();
		}
		SceneGraph* Entity::GetScene() const
		{
			return Scene;
		}
		Entity* Entity::GetParent() const
		{
			auto* Root = Transform->GetRoot();
			if (!Root)
				return nullptr;

			return (Entity*)Root->Ptr<Engine::Entity>();
		}
		Entity* Entity::GetChild(size_t Index) const
		{
			auto* Child = Transform->GetChild(Index);
			if (!Child)
				return nullptr;

			return (Entity*)Child->Ptr<Engine::Entity>();
		}
		Compute::Transform* Entity::GetTransform() const
		{
			return Transform;
		}
		const std::string& Entity::GetName() const
		{
			return Name;
		}
		size_t Entity::GetChildsCount() const
		{
			return Transform->GetChildsCount();
		}
		bool Entity::IsDirty(bool Reset)
		{
			if (!Reset)
				return Dirty;

			bool Result = Dirty;
			Dirty = false;

			return Result;
		}
		bool Entity::IsActive() const
		{
			return Active;
		}

		Renderer::Renderer(RenderSystem* Lab) : System(Lab), Active(true)
		{
			TH_ASSERT_V(Lab != nullptr, "render system should be set");
		}
		Renderer::~Renderer()
		{
		}
		void Renderer::Deserialize(ContentManager* Content, Core::Document* Node)
		{
		}
		void Renderer::Serialize(ContentManager* Content, Core::Document* Node)
		{
		}
		void Renderer::CullGeometry(const Viewer& View)
		{
		}
		void Renderer::ResizeBuffers()
		{
		}
		void Renderer::Activate()
		{
		}
		void Renderer::Deactivate()
		{
		}
		void Renderer::Render(Core::Timer* TimeStep, RenderState State, RenderOpt Options)
		{
		}
		void Renderer::SetRenderer(RenderSystem* NewSystem)
		{
			TH_ASSERT_V(NewSystem != nullptr, "render system should be set");
			System = NewSystem;
		}
		RenderSystem* Renderer::GetRenderer()
		{
			return System;
		}

		ShaderCache::ShaderCache(Graphics::GraphicsDevice* NewDevice) : Device(NewDevice)
		{
		}
		ShaderCache::~ShaderCache()
		{
			ClearCache();
		}
		Graphics::Shader* ShaderCache::Compile(const std::string& Name, const Graphics::Shader::Desc& Desc, size_t BufferSize)
		{
			Graphics::Shader* Shader = Get(Name);
			if (Shader != nullptr)
				return Shader;

			Shader = Device->CreateShader(Desc);
			if (!Shader->IsValid())
			{
				TH_RELEASE(Shader);
				return nullptr;
			}
			else if (BufferSize > 0)
				Device->UpdateBufferSize(Shader, BufferSize);

			Safe.lock();
			SCache& Result = Cache[Name];
			Result.Shader = Shader;
			Result.Count = 1;
			Safe.unlock();

			return Shader;
		}
		Graphics::Shader* ShaderCache::Get(const std::string& Name)
		{
			Safe.lock();
			auto It = Cache.find(Name);
			if (It != Cache.end())
			{
				It->second.Count++;
				Safe.unlock();

				return It->second.Shader;
			}

			Safe.unlock();
			return nullptr;
		}
		std::string ShaderCache::Find(Graphics::Shader* Shader)
		{
			TH_ASSERT(Shader != nullptr, std::string(), "shader should be set");
			Safe.lock();
			for (auto& Item : Cache)
			{
				if (Item.second.Shader == Shader)
				{
					std::string Result = Item.first;
					Safe.unlock();
					return Result;
				}
			}

			Safe.unlock();
			return std::string();
		}
		bool ShaderCache::Has(const std::string& Name)
		{
			Safe.lock();
			auto It = Cache.find(Name);
			if (It != Cache.end())
			{
				Safe.unlock();
				return true;
			}

			Safe.unlock();
			return false;
		}
		bool ShaderCache::Free(const std::string& Name, Graphics::Shader* Shader)
		{
			Safe.lock();
			auto It = Cache.find(Name);
			if (It == Cache.end())
			{
				Safe.unlock();
				return false;
			}

			if (Shader != nullptr && Shader != It->second.Shader)
			{
				Safe.unlock();
				return false;
			}

			It->second.Count--;
			if (It->second.Count > 0)
			{
				Safe.unlock();
				return true;
			}

			TH_RELEASE(It->second.Shader);
			Cache.erase(It);
			Safe.unlock();

			return true;
		}
		void ShaderCache::ClearCache()
		{
			Safe.lock();
			for (auto It = Cache.begin(); It != Cache.end(); ++It)
				TH_CLEAR(It->second.Shader);

			Cache.clear();
			Safe.unlock();
		}

		PrimitiveCache::PrimitiveCache(Graphics::GraphicsDevice* Ref) : Device(Ref), Quad(nullptr)
		{
			Sphere[0] = Sphere[1] = nullptr;
			Cube[0] = Cube[1] = nullptr;
			Box[0] = Box[1] = nullptr;
			SkinBox[0] = SkinBox[1] = nullptr;
		}
		PrimitiveCache::~PrimitiveCache()
		{
			TH_RELEASE(Sphere[(size_t)BufferType::Index]);
			TH_RELEASE(Sphere[(size_t)BufferType::Vertex]);
			TH_RELEASE(Cube[(size_t)BufferType::Index]);
			TH_RELEASE(Cube[(size_t)BufferType::Vertex]);
			TH_RELEASE(Box[(size_t)BufferType::Index]);
			TH_RELEASE(Box[(size_t)BufferType::Vertex]);
			TH_RELEASE(SkinBox[(size_t)BufferType::Index]);
			TH_RELEASE(SkinBox[(size_t)BufferType::Vertex]);
			TH_RELEASE(Quad);
			ClearCache();
		}
		bool PrimitiveCache::Compile(Graphics::ElementBuffer** Results, const std::string& Name, size_t ElementSize, size_t ElementsCount)
		{
			TH_ASSERT(Results != nullptr, false, "results should be set");
			if (Get(Results, Name))
				return false;

			Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
			F.AccessFlags = Graphics::CPUAccess::Write;
			F.Usage = Graphics::ResourceUsage::Dynamic;
			F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
			F.ElementWidth = ElementSize;
			F.ElementCount = ElementsCount;

			Graphics::ElementBuffer* VertexBuffer = Device->CreateElementBuffer(F);
			if (!VertexBuffer)
				return false;

			F = Graphics::ElementBuffer::Desc();
			F.AccessFlags = Graphics::CPUAccess::Write;
			F.Usage = Graphics::ResourceUsage::Dynamic;
			F.BindFlags = Graphics::ResourceBind::Index_Buffer;
			F.ElementWidth = sizeof(int);
			F.ElementCount = ElementsCount * 3;

			Graphics::ElementBuffer* IndexBuffer = Device->CreateElementBuffer(F);
			if (!IndexBuffer)
			{
				TH_RELEASE(VertexBuffer);
				return false;
			}

			Safe.lock();
			SCache& Result = Cache[Name];
			Result.Buffers[(size_t)BufferType::Index] = Results[(size_t)BufferType::Index] = IndexBuffer;
			Result.Buffers[(size_t)BufferType::Vertex] = Results[(size_t)BufferType::Vertex] = VertexBuffer;
			Result.Count = 1;
			Safe.unlock();

			return true;
		}
		bool PrimitiveCache::Get(Graphics::ElementBuffer** Results, const std::string& Name)
		{
			TH_ASSERT(Results != nullptr, false, "results should be set");
			Safe.lock();
			auto It = Cache.find(Name);
			if (It != Cache.end())
			{
				It->second.Count++;
				Safe.unlock();

				Results[(size_t)BufferType::Index] = It->second.Buffers[(size_t)BufferType::Index];
				Results[(size_t)BufferType::Vertex] = It->second.Buffers[(size_t)BufferType::Vertex];
				return true;
			}

			Safe.unlock();
			return false;
		}
		bool PrimitiveCache::Has(const std::string& Name)
		{
			Safe.lock();
			auto It = Cache.find(Name);
			if (It != Cache.end())
			{
				Safe.unlock();
				return true;
			}

			Safe.unlock();
			return false;
		}
		bool PrimitiveCache::Free(const std::string& Name, Graphics::ElementBuffer** Buffers)
		{
			Safe.lock();
			auto It = Cache.find(Name);
			if (It == Cache.end())
			{
				Safe.unlock();
				return false;
			}

			if (Buffers != nullptr)
			{
				if ((Buffers[0] != nullptr && Buffers[0] != It->second.Buffers[0]) || (Buffers[1] != nullptr && Buffers[1] != It->second.Buffers[1]))
				{
					Safe.unlock();
					return false;
				}
			}

			It->second.Count--;
			if (It->second.Count > 0)
			{
				Safe.unlock();
				return true;
			}

			TH_RELEASE(It->second.Buffers[0]);
			TH_RELEASE(It->second.Buffers[1]);
			Cache.erase(It);
			Safe.unlock();

			return true;
		}
		std::string PrimitiveCache::Find(Graphics::ElementBuffer** Buffers)
		{
			TH_ASSERT(Buffers != nullptr, std::string(), "buffers should be set");
			Safe.lock();
			for (auto& Item : Cache)
			{
				if (Item.second.Buffers[0] == Buffers[0] && Item.second.Buffers[1] == Buffers[1])
				{
					std::string Result = Item.first;
					Safe.unlock();
					return Result;
				}
			}

			Safe.unlock();
			return std::string();
		}
		Graphics::ElementBuffer* PrimitiveCache::GetQuad()
		{
			TH_ASSERT(Device != nullptr, nullptr, "graphics device should be set");
			if (Quad != nullptr)
				return Quad;

			std::vector<Compute::ShapeVertex> Elements;
			Elements.push_back({ -1.0f, -1.0f, 0, -1, 0 });
			Elements.push_back({ -1.0f, 1.0f, 0, -1, -1 });
			Elements.push_back({ 1.0f, 1.0f, 0, 0, -1 });
			Elements.push_back({ -1.0f, -1.0f, 0, -1, 0 });
			Elements.push_back({ 1.0f, 1.0f, 0, 0, -1 });
			Elements.push_back({ 1.0f, -1.0f, 0, 0, 0 });
			Compute::Common::TexCoordRhToLh(Elements);

			Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
			F.AccessFlags = Graphics::CPUAccess::Invalid;
			F.Usage = Graphics::ResourceUsage::Default;
			F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
			F.ElementCount = 6;
			F.ElementWidth = sizeof(Compute::ShapeVertex);
			F.Elements = &Elements[0];

			Safe.lock();
			Quad = Device->CreateElementBuffer(F);
			Safe.unlock();

			return Quad;
		}
		Graphics::ElementBuffer* PrimitiveCache::GetSphere(BufferType Type)
		{
			TH_ASSERT(Device != nullptr, nullptr, "graphics device should be set");
			if (Sphere[(size_t)Type] != nullptr)
				return Sphere[(size_t)Type];

			if (Type == BufferType::Index)
			{
				std::vector<int> Indices;
				Indices.push_back(0);
				Indices.push_back(4);
				Indices.push_back(1);
				Indices.push_back(0);
				Indices.push_back(9);
				Indices.push_back(4);
				Indices.push_back(9);
				Indices.push_back(5);
				Indices.push_back(4);
				Indices.push_back(4);
				Indices.push_back(5);
				Indices.push_back(8);
				Indices.push_back(4);
				Indices.push_back(8);
				Indices.push_back(1);
				Indices.push_back(8);
				Indices.push_back(10);
				Indices.push_back(1);
				Indices.push_back(8);
				Indices.push_back(3);
				Indices.push_back(10);
				Indices.push_back(5);
				Indices.push_back(3);
				Indices.push_back(8);
				Indices.push_back(5);
				Indices.push_back(2);
				Indices.push_back(3);
				Indices.push_back(2);
				Indices.push_back(7);
				Indices.push_back(3);
				Indices.push_back(7);
				Indices.push_back(10);
				Indices.push_back(3);
				Indices.push_back(7);
				Indices.push_back(6);
				Indices.push_back(10);
				Indices.push_back(7);
				Indices.push_back(11);
				Indices.push_back(6);
				Indices.push_back(11);
				Indices.push_back(0);
				Indices.push_back(6);
				Indices.push_back(0);
				Indices.push_back(1);
				Indices.push_back(6);
				Indices.push_back(6);
				Indices.push_back(1);
				Indices.push_back(10);
				Indices.push_back(9);
				Indices.push_back(0);
				Indices.push_back(11);
				Indices.push_back(9);
				Indices.push_back(11);
				Indices.push_back(2);
				Indices.push_back(9);
				Indices.push_back(2);
				Indices.push_back(5);
				Indices.push_back(7);
				Indices.push_back(2);
				Indices.push_back(11);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Index_Buffer;
				F.ElementCount = (unsigned int)Indices.size();
				F.ElementWidth = sizeof(int);
				F.Elements = &Indices[0];

				Safe.lock();
				Sphere[(size_t)BufferType::Index] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Sphere[(size_t)BufferType::Index];
			}
			else if (Type == BufferType::Vertex)
			{
				const float X = 0.525731112119133606;
				const float Z = 0.850650808352039932;
				const float N = 0.0f;

				std::vector<Compute::ShapeVertex> Elements;
				Elements.push_back({ -X, N, Z });
				Elements.push_back({ X, N, Z });
				Elements.push_back({ -X, N, -Z });
				Elements.push_back({ X, N, -Z });
				Elements.push_back({ N, Z, X });
				Elements.push_back({ N, Z, -X });
				Elements.push_back({ N, -Z, X });
				Elements.push_back({ N, -Z, -X });
				Elements.push_back({ Z, X, N });
				Elements.push_back({ -Z, X, N });
				Elements.push_back({ Z, -X, N });
				Elements.push_back({ -Z, -X, N });

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
				F.ElementCount = (unsigned int)Elements.size();
				F.ElementWidth = sizeof(Compute::ShapeVertex);
				F.Elements = &Elements[0];

				Safe.lock();
				Sphere[(size_t)BufferType::Vertex] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Sphere[(size_t)BufferType::Vertex];
			}

			return nullptr;
		}
		Graphics::ElementBuffer* PrimitiveCache::GetCube(BufferType Type)
		{
			TH_ASSERT(Device != nullptr, nullptr, "graphics device should be set");
			if (Cube[(size_t)Type] != nullptr)
				return Cube[(size_t)Type];

			if (Type == BufferType::Index)
			{
				std::vector<int> Indices;
				Indices.push_back(0);
				Indices.push_back(1);
				Indices.push_back(2);
				Indices.push_back(0);
				Indices.push_back(18);
				Indices.push_back(1);
				Indices.push_back(3);
				Indices.push_back(4);
				Indices.push_back(5);
				Indices.push_back(3);
				Indices.push_back(19);
				Indices.push_back(4);
				Indices.push_back(6);
				Indices.push_back(7);
				Indices.push_back(8);
				Indices.push_back(6);
				Indices.push_back(20);
				Indices.push_back(7);
				Indices.push_back(9);
				Indices.push_back(10);
				Indices.push_back(11);
				Indices.push_back(9);
				Indices.push_back(21);
				Indices.push_back(10);
				Indices.push_back(12);
				Indices.push_back(13);
				Indices.push_back(14);
				Indices.push_back(12);
				Indices.push_back(22);
				Indices.push_back(13);
				Indices.push_back(15);
				Indices.push_back(16);
				Indices.push_back(17);
				Indices.push_back(15);
				Indices.push_back(23);
				Indices.push_back(16);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Index_Buffer;
				F.ElementCount = (unsigned int)Indices.size();
				F.ElementWidth = sizeof(int);
				F.Elements = &Indices[0];

				Safe.lock();
				Cube[(size_t)BufferType::Index] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Cube[(size_t)BufferType::Index];
			}
			else if (Type == BufferType::Vertex)
			{
				std::vector<Compute::ShapeVertex> Elements;
				Elements.push_back({ -1, 1, 1, 0.875, -0.5 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75 });
				Elements.push_back({ -1, -1, -1, 0.375, -1 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75 });
				Elements.push_back({ -1, -1, 1, 0.625, -0 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25 });
				Elements.push_back({ -1, -1, -1, 0.375, -0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5 });
				Elements.push_back({ -1, -1, -1, 0.125, -0.75 });
				Elements.push_back({ -1, 1, -1, 0.125, -0.5 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25 });
				Elements.push_back({ -1, -1, 1, 0.875, -0.75 });
				Elements.push_back({ -1, -1, 1, 0.625, -1 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5 });
				Compute::Common::TexCoordRhToLh(Elements);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
				F.ElementCount = (unsigned int)Elements.size();
				F.ElementWidth = sizeof(Compute::ShapeVertex);
				F.Elements = &Elements[0];

				Safe.lock();
				Cube[(size_t)BufferType::Vertex] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Cube[(size_t)BufferType::Vertex];
			}

			return nullptr;
		}
		Graphics::ElementBuffer* PrimitiveCache::GetBox(BufferType Type)
		{
			TH_ASSERT(Device != nullptr, nullptr, "graphics device should be set");
			if (Box[(size_t)Type] != nullptr)
				return Box[(size_t)Type];

			if (Type == BufferType::Index)
			{
				std::vector<int> Indices;
				Indices.insert(Indices.begin(), 0);
				Indices.insert(Indices.begin(), 1);
				Indices.insert(Indices.begin(), 2);
				Indices.insert(Indices.begin(), 0);
				Indices.insert(Indices.begin(), 18);
				Indices.insert(Indices.begin(), 1);
				Indices.insert(Indices.begin(), 3);
				Indices.insert(Indices.begin(), 4);
				Indices.insert(Indices.begin(), 5);
				Indices.insert(Indices.begin(), 3);
				Indices.insert(Indices.begin(), 19);
				Indices.insert(Indices.begin(), 4);
				Indices.insert(Indices.begin(), 6);
				Indices.insert(Indices.begin(), 7);
				Indices.insert(Indices.begin(), 8);
				Indices.insert(Indices.begin(), 6);
				Indices.insert(Indices.begin(), 20);
				Indices.insert(Indices.begin(), 7);
				Indices.insert(Indices.begin(), 9);
				Indices.insert(Indices.begin(), 10);
				Indices.insert(Indices.begin(), 11);
				Indices.insert(Indices.begin(), 9);
				Indices.insert(Indices.begin(), 21);
				Indices.insert(Indices.begin(), 10);
				Indices.insert(Indices.begin(), 12);
				Indices.insert(Indices.begin(), 13);
				Indices.insert(Indices.begin(), 14);
				Indices.insert(Indices.begin(), 12);
				Indices.insert(Indices.begin(), 22);
				Indices.insert(Indices.begin(), 13);
				Indices.insert(Indices.begin(), 15);
				Indices.insert(Indices.begin(), 16);
				Indices.insert(Indices.begin(), 17);
				Indices.insert(Indices.begin(), 15);
				Indices.insert(Indices.begin(), 23);
				Indices.insert(Indices.begin(), 16);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Index_Buffer;
				F.ElementCount = (unsigned int)Indices.size();
				F.ElementWidth = sizeof(int);
				F.Elements = &Indices[0];

				Safe.lock();
				Box[(size_t)BufferType::Index] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Box[(size_t)BufferType::Index];
			}
			else if (Type == BufferType::Vertex)
			{
				std::vector<Compute::Vertex> Elements;
				Elements.push_back({ -1, 1, 1, 0.875, -0.5, 0, 0, 1, -1, 0, 0, 0, 1, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 0, 0, 1, -1, 0, 0, 0, 1, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 0, 0, 1, -1, 0, 0, 0, 1, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 0, -1, 0, 0, 0, 1, 1, 0, 0 });
				Elements.push_back({ -1, -1, -1, 0.375, -1, 0, -1, 0, 0, 0, 1, 1, 0, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 0, -1, 0, 0, 0, 1, 1, 0, 0 });
				Elements.push_back({ -1, -1, 1, 0.625, -0, -1, 0, 0, 0, 0, 1, 0, -1, 0 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25, -1, 0, 0, 0, 0, 1, 0, -1, 0 });
				Elements.push_back({ -1, -1, -1, 0.375, -0, -1, 0, 0, 0, 0, 1, 0, -1, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 0, 0, -1, 1, 0, 0, 0, 1, 0 });
				Elements.push_back({ -1, -1, -1, 0.125, -0.75, 0, 0, -1, 1, 0, 0, 0, 1, 0 });
				Elements.push_back({ -1, 1, -1, 0.125, -0.5, 0, 0, -1, 1, 0, 0, 0, 1, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 1, 0, 0, 0, 0, 1, 0, 1, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 1, 0, 0, 0, 0, 1, 0, 1, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 1, 0, 0, 0, 0, 1, 0, 1, 0 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25, 0, 1, 0, 0, 0, 1, -1, 0, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 0, 1, 0, 0, 0, 1, -1, 0, 0 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25, 0, 1, 0, 0, 0, 1, -1, 0, 0 });
				Elements.push_back({ -1, -1, 1, 0.875, -0.75, 0, 0, 1, -1, 0, 0, 0, 1, 0 });
				Elements.push_back({ -1, -1, 1, 0.625, -1, 0, -1, 0, 0, 0, 1, 1, 0, 0 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25, -1, 0, 0, 0, 0, 1, 0, -1, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 0, 0, -1, 1, 0, 0, 0, 1, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 1, 0, 0, 0, 0, 1, 0, 1, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 0, 1, 0, 0, 0, 1, -1, 0, 0 });
				Compute::Common::TexCoordRhToLh(Elements);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
				F.ElementCount = (unsigned int)Elements.size();
				F.ElementWidth = sizeof(Compute::Vertex);
				F.Elements = &Elements[0];

				Safe.lock();
				Box[(size_t)BufferType::Vertex] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return Box[(size_t)BufferType::Vertex];
			}

			return nullptr;
		}
		Graphics::ElementBuffer* PrimitiveCache::GetSkinBox(BufferType Type)
		{
			TH_ASSERT(Device != nullptr, nullptr, "graphics device should be set");
			if (SkinBox[(size_t)Type] != nullptr)
				return SkinBox[(size_t)Type];

			if (Type == BufferType::Index)
			{
				std::vector<int> Indices;
				Indices.insert(Indices.begin(), 0);
				Indices.insert(Indices.begin(), 1);
				Indices.insert(Indices.begin(), 2);
				Indices.insert(Indices.begin(), 0);
				Indices.insert(Indices.begin(), 18);
				Indices.insert(Indices.begin(), 1);
				Indices.insert(Indices.begin(), 3);
				Indices.insert(Indices.begin(), 4);
				Indices.insert(Indices.begin(), 5);
				Indices.insert(Indices.begin(), 3);
				Indices.insert(Indices.begin(), 19);
				Indices.insert(Indices.begin(), 4);
				Indices.insert(Indices.begin(), 6);
				Indices.insert(Indices.begin(), 7);
				Indices.insert(Indices.begin(), 8);
				Indices.insert(Indices.begin(), 6);
				Indices.insert(Indices.begin(), 20);
				Indices.insert(Indices.begin(), 7);
				Indices.insert(Indices.begin(), 9);
				Indices.insert(Indices.begin(), 10);
				Indices.insert(Indices.begin(), 11);
				Indices.insert(Indices.begin(), 9);
				Indices.insert(Indices.begin(), 21);
				Indices.insert(Indices.begin(), 10);
				Indices.insert(Indices.begin(), 12);
				Indices.insert(Indices.begin(), 13);
				Indices.insert(Indices.begin(), 14);
				Indices.insert(Indices.begin(), 12);
				Indices.insert(Indices.begin(), 22);
				Indices.insert(Indices.begin(), 13);
				Indices.insert(Indices.begin(), 15);
				Indices.insert(Indices.begin(), 16);
				Indices.insert(Indices.begin(), 17);
				Indices.insert(Indices.begin(), 15);
				Indices.insert(Indices.begin(), 23);
				Indices.insert(Indices.begin(), 16);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Index_Buffer;
				F.ElementCount = (unsigned int)Indices.size();
				F.ElementWidth = sizeof(int);
				F.Elements = &Indices[0];

				Safe.lock();
				SkinBox[(size_t)BufferType::Index] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return SkinBox[(size_t)BufferType::Index];
			}
			else if (Type == BufferType::Vertex)
			{
				std::vector<Compute::SkinVertex> Elements;
				Elements.push_back({ -1, 1, 1, 0.875, -0.5, 0, 0, 1, -1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 0, 0, 1, -1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 0, 0, 1, -1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 0, -1, 0, 0, 0, 1, 1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, -1, 0.375, -1, 0, -1, 0, 0, 0, 1, 1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 0, -1, 0, 0, 0, 1, 1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, 1, 0.625, -0, -1, 0, 0, 0, 0, 1, 0, -1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25, -1, 0, 0, 0, 0, 1, 0, -1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, -1, 0.375, -0, -1, 0, 0, 0, 0, 1, 0, -1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 0, 0, -1, 1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, -1, 0.125, -0.75, 0, 0, -1, 1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, 1, -1, 0.125, -0.5, 0, 0, -1, 1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 1, 0, 0, 0, 0, 1, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 1, 0, 0, 0, 0, 1, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 1, 0, 0, 0, 0, 1, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25, 0, 1, 0, 0, 0, 1, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, -1, 0.375, -0.5, 0, 1, 0, 0, 0, 1, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, 1, -1, 0.375, -0.25, 0, 1, 0, 0, 0, 1, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, 1, 0.875, -0.75, 0, 0, 1, -1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, -1, 1, 0.625, -1, 0, -1, 0, 0, 0, 1, 1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ -1, 1, 1, 0.625, -0.25, -1, 0, 0, 0, 0, 1, 0, -1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, -1, 0.375, -0.75, 0, 0, -1, 1, 0, 0, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, -1, 1, 0.625, -0.75, 1, 0, 0, 0, 0, 1, 0, 1, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Elements.push_back({ 1, 1, 1, 0.625, -0.5, 0, 1, 0, 0, 0, 1, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, 0 });
				Compute::Common::TexCoordRhToLh(Elements);

				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Invalid;
				F.Usage = Graphics::ResourceUsage::Default;
				F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
				F.ElementCount = (unsigned int)Elements.size();
				F.ElementWidth = sizeof(Compute::SkinVertex);
				F.Elements = &Elements[0];

				Safe.lock();
				SkinBox[(size_t)BufferType::Vertex] = Device->CreateElementBuffer(F);
				Safe.unlock();

				return SkinBox[(size_t)BufferType::Vertex];
			}

			return nullptr;
		}
		void PrimitiveCache::GetSphereBuffers(Graphics::ElementBuffer** Result)
		{
			TH_ASSERT_V(Result != nullptr, "result should be set");
			Result[(size_t)BufferType::Index] = GetSphere(BufferType::Index);
			Result[(size_t)BufferType::Vertex] = GetSphere(BufferType::Vertex);
		}
		void PrimitiveCache::GetCubeBuffers(Graphics::ElementBuffer** Result)
		{
			TH_ASSERT_V(Result != nullptr, "result should be set");
			Result[(size_t)BufferType::Index] = GetCube(BufferType::Index);
			Result[(size_t)BufferType::Vertex] = GetCube(BufferType::Vertex);
		}
		void PrimitiveCache::GetBoxBuffers(Graphics::ElementBuffer** Result)
		{
			TH_ASSERT_V(Result != nullptr, "result should be set");
			Result[(size_t)BufferType::Index] = GetBox(BufferType::Index);
			Result[(size_t)BufferType::Vertex] = GetBox(BufferType::Vertex);
		}
		void PrimitiveCache::GetSkinBoxBuffers(Graphics::ElementBuffer** Result)
		{
			TH_ASSERT_V(Result != nullptr, "result should be set");
			Result[(size_t)BufferType::Index] = GetSkinBox(BufferType::Index);
			Result[(size_t)BufferType::Vertex] = GetSkinBox(BufferType::Vertex);
		}
		void PrimitiveCache::ClearCache()
		{
			Safe.lock();
			for (auto It = Cache.begin(); It != Cache.end(); ++It)
			{
				TH_CLEAR(It->second.Buffers[0]);
				TH_CLEAR(It->second.Buffers[1]);
			}

			Cache.clear();
			TH_CLEAR(Sphere[0]);
			TH_CLEAR(Sphere[1]);
			TH_CLEAR(Cube[0]);
			TH_CLEAR(Cube[1]);
			TH_CLEAR(Box[0]);
			TH_CLEAR(Box[1]);
			TH_CLEAR(SkinBox[0]);
			TH_CLEAR(SkinBox[1]);
			TH_CLEAR(Quad);
			Safe.unlock();
		}

		RenderSystem::RenderSystem(SceneGraph* NewScene) : DepthStencil(nullptr), Blend(nullptr), Sampler(nullptr), Target(nullptr), Device(nullptr), BaseMaterial(nullptr), Scene(NewScene), OcclusionCulling(false), FrustumCulling(true), PreciseCulling(false)
		{
			Occlusion.Delay = 5;
			Sorting.Delay = 5;
			StallFrames = 1;
			DepthSize = 128;
			Satisfied = true;

			TH_ASSERT_V(NewScene != nullptr, "scene should be set");
			TH_ASSERT_V(NewScene->GetDevice() != nullptr, "graphics device should be set");
			Device = NewScene->GetDevice();
			SetDepthSize(DepthSize);
		}
		RenderSystem::~RenderSystem()
		{
			for (auto& Renderer : Renderers)
			{
				if (!Renderer)
					continue;

				Renderer->Deactivate();
				TH_RELEASE(Renderer);
			}

			TH_RELEASE(Target);
		}
		void RenderSystem::SetOcclusionCulling(bool Enabled, bool KeepResults)
		{
			OcclusionCulling = Enabled;
			if (!KeepResults)
				ClearCull();
		}
		void RenderSystem::SetFrustumCulling(bool Enabled, bool KeepResults)
		{
			FrustumCulling = Enabled;
			if (!KeepResults)
				ClearCull();
		}
		void RenderSystem::SetDepthSize(size_t Size)
		{
			DepthStencil = Device->GetDepthStencilState("less-no-stencil");
			Blend = Device->GetBlendState("overwrite-colorless");
			Sampler = Device->GetSamplerState("point");
			DepthSize = Size;

			Graphics::MultiRenderTarget2D* MRT = Scene->GetMRT(TargetType::Main);
			float Aspect = (float)MRT->GetWidth() / MRT->GetHeight();

			Graphics::DepthTarget2D::Desc I;
			I.Width = (size_t)((float)Size * Aspect);
			I.Height = Size;

			TH_RELEASE(Target);
			Target = Device->CreateDepthTarget2D(I);
		}
		void RenderSystem::SetView(const Compute::Matrix4x4& _View, const Compute::Matrix4x4& _Projection, const Compute::Vector3& _Position, float _Near, float _Far, bool Upload)
		{
			View.Set(_View, _Projection, _Position, _Near, _Far);
			if (Upload)
				RestoreViewBuffer(&View);
		}
		void RenderSystem::RestoreViewBuffer(Viewer* Buffer)
		{
			TH_ASSERT_V(Device != nullptr, "graphics device should be set");
			if (&View != Buffer)
			{
				if (Buffer == nullptr)
				{
					auto* Viewer = (Components::Camera*)Scene->Camera.load();
					if (Viewer != nullptr)
						Viewer->GetViewer(&View);
				}
				else
					View = *Buffer;
			}

			Device->View.InvViewProj = View.InvViewProjection;
			Device->View.ViewProj = View.ViewProjection;
			Device->View.Proj = View.Projection;
			Device->View.View = View.View;
			Device->View.Position = View.Position;
			Device->View.Direction = View.Rotation.dDirection();
			Device->View.Far = View.FarPlane;
			Device->View.Near = View.NearPlane;
			Device->UpdateBuffer(Graphics::RenderBufferType::View);
		}
		void RenderSystem::Render(Core::Timer* Time, RenderState Stage, RenderOpt Options)
		{
			TH_ASSERT_V(Time != nullptr, "timer should be set");
			for (auto& Next : Renderers)
			{
				if (Next->Active)
					Next->Render(Time, Stage, Options);
			}
		}
		void RenderSystem::Remount(Renderer* fTarget)
		{
			TH_ASSERT_V(fTarget != nullptr, "renderer should be set");
			fTarget->Deactivate();
			fTarget->SetRenderer(this);
			fTarget->Activate();
			fTarget->ResizeBuffers();
		}
		void RenderSystem::Remount()
		{
			ClearCull();
			for (auto& Target : Renderers)
				Remount(Target);
		}
		void RenderSystem::Mount()
		{
			for (auto& Renderer : Renderers)
				Renderer->Activate();
		}
		void RenderSystem::Unmount()
		{
			for (auto& Renderer : Renderers)
				Renderer->Deactivate();
		}
		void RenderSystem::ClearCull()
		{
			TH_PPUSH("rs-clear-cull", TH_PERF_FRAME);
			for (auto& Base : Cull)
			{
				auto* Array = Scene->GetComponents(Base);
				for (auto It = Array->Begin(); It != Array->End(); ++It)
					((Cullable*)*It)->ClearCull();
			}
			TH_PPOP();
		}
		void RenderSystem::Synchronize(Core::Timer* Time, double Begin, double End, bool WasDirty)
		{
			TH_ASSERT_V(Begin >= 0.0 && Begin <= End, "invalid begin range");
			TH_ASSERT_V(End >= 0.0 && End >= Begin && End <= 1.0, "invalid end range");

			if (!FrustumCulling)
				return;

			TH_PPUSH("rs-sync", TH_PERF_FRAME);
			for (auto& Base : Cull)
			{
				auto* Array = Scene->GetComponents(Base);
				double Size = (double)Array->Size();
				uint64_t IdxBegin = (uint64_t)std::min(Size, std::ceil(Size * Begin));
				uint64_t IdxEnd = (uint64_t)std::min(Size, std::ceil(Size * (1.0 - End)));
				auto* First = Array->Begin() + IdxBegin;
				auto* Last = Array->End() - IdxEnd;
				for (auto It = First; It != Last; ++It)
				{
					Cullable* Data = (Cullable*)*It;
					if (WasDirty || Data->GetEntity()->GetTransform()->IsDirty())
						Data->Visibility = Data->Cull(View);
				}
			}
			TH_PPOP();
		}
		void RenderSystem::CullGeometry(Core::Timer* Time)
		{
			if (!OcclusionCulling || !Target)
				return;

			double ElapsedTime = Time->GetElapsedTime();
			if (Sorting.TickEvent(ElapsedTime))
			{
				for (auto& Base : Cull)
					Scene->SortFrontToBack(Scene->GetOpaque(Base));
			}

			if (!Occlusion.TickEvent(ElapsedTime))
				return;

			Dirty = Scene->GetCamera()->GetEntity()->IsDirty(true);
			Device->SetDepthStencilState(DepthStencil);
			Device->SetBlendState(Blend);
			Device->SetTarget(Target);
			Device->ClearDepth(Target);

			for (auto& Renderer : Renderers)
			{
				if (Renderer->Active)
					Renderer->CullGeometry(View);
			}
		}
		void RenderSystem::MoveRenderer(uint64_t Id, int64_t Offset)
		{
			if (Offset == 0)
				return;

			for (int64_t i = 0; i < Renderers.size(); i++)
			{
				if (Renderers[i]->GetId() != Id)
					continue;

				if (i + Offset < 0 || i + Offset >= Renderers.size())
					return;

				Renderer* Swap = Renderers[i + Offset];
				Renderers[i + Offset] = Renderers[i];
				Renderers[i] = Swap;
				return;
			}
		}
		void RenderSystem::RemoveRenderer(uint64_t Id)
		{
			for (auto It = Renderers.begin(); It != Renderers.end(); ++It)
			{
				if (*It && (*It)->GetId() == Id)
				{
					(*It)->Deactivate();
					TH_RELEASE(*It);
					Renderers.erase(It);
					break;
				}
			}
		}
		void RenderSystem::RestoreOutput()
		{
			Scene->SetMRT(TargetType::Main, false);
		}
		void RenderSystem::FreeShader(const std::string& Name, Graphics::Shader* Shader)
		{
			ShaderCache* Cache = Scene->GetShaders();
			if (Cache != nullptr)
			{
				if (Cache->Has(Name))
					return;
			}

			TH_RELEASE(Shader);
		}
		void RenderSystem::FreeShader(Graphics::Shader* Shader)
		{
			ShaderCache* Cache = Scene->GetShaders();
			if (Cache != nullptr)
				return FreeShader(Cache->Find(Shader), Shader);

			TH_RELEASE(Shader);
		}
		void RenderSystem::FreeBuffers(const std::string& Name, Graphics::ElementBuffer** Buffers)
		{
			if (!Buffers)
				return;

			PrimitiveCache* Cache = Scene->GetPrimitives();
			if (Cache != nullptr)
			{
				if (Cache->Has(Name))
					return;
			}

			TH_RELEASE(Buffers[0]);
			TH_RELEASE(Buffers[1]);
		}
		void RenderSystem::FreeBuffers(Graphics::ElementBuffer** Buffers)
		{
			if (!Buffers)
				return;

			PrimitiveCache* Cache = Scene->GetPrimitives();
			if (Cache != nullptr)
				return FreeBuffers(Cache->Find(Buffers), Buffers);

			TH_RELEASE(Buffers[0]);
			TH_RELEASE(Buffers[1]);
		}
		void RenderSystem::ClearMaterials()
		{
			BaseMaterial = nullptr;
		}
		bool RenderSystem::PushGeometryBuffer(Material* Next)
		{
			if (!Next)
				return false;

			if (Next == BaseMaterial)
				return true;

			BaseMaterial = Next;
			Device->SetTexture2D(Next->DiffuseMap, 1, TH_PS);
			Device->SetTexture2D(Next->NormalMap, 2, TH_PS);
			Device->SetTexture2D(Next->MetallicMap, 3, TH_PS);
			Device->SetTexture2D(Next->RoughnessMap, 4, TH_PS);
			Device->SetTexture2D(Next->HeightMap, 5, TH_PS);
			Device->SetTexture2D(Next->OcclusionMap, 6, TH_PS);
			Device->SetTexture2D(Next->EmissionMap, 7, TH_PS);
			Device->Render.Diffuse = (float)(Next->DiffuseMap != nullptr);
			Device->Render.Normal = (float)(Next->NormalMap != nullptr);
			Device->Render.Height = (float)(Next->HeightMap != nullptr);
			Device->Render.Mid = (float)Next->Slot;

			return true;
		}
		bool RenderSystem::PushVoxelsBuffer(Material* Next)
		{
			if (!Next || Next->Surface.Transparency > 0.0f)
				return false;

			if (Next == BaseMaterial)
				return true;

			BaseMaterial = Next;
			Device->SetTexture2D(Next->DiffuseMap, 4, TH_PS);
			Device->SetTexture2D(Next->NormalMap, 5, TH_PS);
			Device->SetTexture2D(Next->MetallicMap, 6, TH_PS);
			Device->SetTexture2D(Next->RoughnessMap, 7, TH_PS);
			Device->SetTexture2D(Next->OcclusionMap, 8, TH_PS);
			Device->SetTexture2D(Next->EmissionMap, 9, TH_PS);
			Device->Render.Diffuse = (float)(Next->DiffuseMap != nullptr);
			Device->Render.Normal = (float)(Next->NormalMap != nullptr);
			Device->Render.Mid = (float)Next->Slot;

			return true;
		}
		bool RenderSystem::PushDepthLinearBuffer(Material* Next)
		{
			if (!Next)
				return false;

			if (Next == BaseMaterial)
				return true;

			BaseMaterial = Next;
			Device->SetTexture2D(Next->DiffuseMap, 1, TH_PS);
			Device->Render.Diffuse = (float)(Next->DiffuseMap != nullptr);
			Device->Render.Mid = (float)Next->Slot;

			return true;
		}
		bool RenderSystem::PushDepthCubicBuffer(Material* Next)
		{
			return PushDepthLinearBuffer(Next);
		}
		bool RenderSystem::PassCullable(Cullable* Base, CullResult Mode, float* Result)
		{
			TH_ASSERT(Base != nullptr, false, "cullable should be set");
			if (Mode == CullResult::Last)
				return Base->Visibility;

			float D = Base->Cull(View);
			if (Mode == CullResult::Cache)
				Base->Visibility = D;

			if (Result != nullptr)
				*Result = D;

			return D > 0.0f;
		}
		bool RenderSystem::PassDrawable(Drawable* Base, CullResult Mode, float* Result)
		{
			TH_ASSERT(Base != nullptr, false, "drawable should be set");
			if (Mode == CullResult::Last)
			{
				if (OcclusionCulling)
				{
					int R = Base->Query.Fetch(this);
					if (R != -1)
						return R > 0;
				}

				if (!Base->Query.GetPassed())
					return false;
			}

			return PassCullable(Base, Mode, Result);
		}
		bool RenderSystem::PassOcclusion(Drawable* Base)
		{
			TH_ASSERT(Base != nullptr, false, "drawable should be set");
			return Dirty || Base->Parent->IsDirty(true) || Base->Query.GetPassed() > 0;
		}
		bool RenderSystem::HasOcclusionCulling()
		{
			return OcclusionCulling;
		}
		bool RenderSystem::HasFrustumCulling()
		{
			return FrustumCulling;
		}
		int64_t RenderSystem::GetOffset(uint64_t Id)
		{
			for (size_t i = 0; i < Renderers.size(); i++)
			{
				if (Renderers[i]->GetId() == Id)
					return (int64_t)i;
			}

			return -1;
		}
		Graphics::Shader* RenderSystem::CompileShader(Graphics::Shader::Desc& Desc, size_t BufferSize)
		{
			TH_ASSERT(!Desc.Filename.empty(), nullptr, "shader must have a name");
			ShaderCache* Cache = Scene->GetShaders();
			if (Cache != nullptr)
				return Cache->Compile(Desc.Filename, Desc, BufferSize);

			Graphics::Shader* Shader = Device->CreateShader(Desc);
			if (BufferSize > 0)
				Device->UpdateBufferSize(Shader, BufferSize);

			return Shader;
		}
		Graphics::Shader* RenderSystem::CompileShader(const std::string& SectionName, size_t BufferSize)
		{
			Graphics::Shader::Desc I = Graphics::Shader::Desc();
			if (!Device->GetSection(SectionName, &I))
				return nullptr;

			return CompileShader(I, BufferSize);
		}
		bool RenderSystem::CompileBuffers(Graphics::ElementBuffer** Result, const std::string& Name, size_t ElementSize, size_t ElementsCount)
		{
			TH_ASSERT(Result != nullptr, nullptr, "result should be set");
			TH_ASSERT(!Name.empty(), nullptr, "buffers must have a name");

			PrimitiveCache* Cache = Scene->GetPrimitives();
			if (Cache != nullptr)
				return Cache->Compile(Result, Name, ElementSize, ElementsCount);

			Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
			F.AccessFlags = Graphics::CPUAccess::Write;
			F.Usage = Graphics::ResourceUsage::Dynamic;
			F.BindFlags = Graphics::ResourceBind::Vertex_Buffer;
			F.ElementWidth = ElementSize;
			F.ElementCount = ElementsCount;

			Graphics::ElementBuffer* VertexBuffer = Device->CreateElementBuffer(F);
			if (!VertexBuffer)
				return false;

			F = Graphics::ElementBuffer::Desc();
			F.AccessFlags = Graphics::CPUAccess::Write;
			F.Usage = Graphics::ResourceUsage::Dynamic;
			F.BindFlags = Graphics::ResourceBind::Index_Buffer;
			F.ElementWidth = sizeof(int);
			F.ElementCount = ElementsCount * 3;

			Graphics::ElementBuffer* IndexBuffer = Device->CreateElementBuffer(F);
			if (!IndexBuffer)
			{
				TH_RELEASE(VertexBuffer);
				return false;
			}

			Result[(size_t)BufferType::Index] = IndexBuffer;
			Result[(size_t)BufferType::Vertex] = VertexBuffer;

			return true;
		}
		Renderer* RenderSystem::AddRenderer(Renderer* In)
		{
			TH_ASSERT(In != nullptr, nullptr, "renderer should be set");
			for (auto It = Renderers.begin(); It != Renderers.end(); ++It)
			{
				if (*It && (*It)->GetId() == In->GetId())
				{
					if (*It == In)
						return In;

					(*It)->Deactivate();
					TH_RELEASE(*It);
					Renderers.erase(It);
					break;
				}
			}

			In->SetRenderer(this);
			In->Activate();
			In->ResizeBuffers();
			Renderers.push_back(In);

			return In;
		}
		Renderer* RenderSystem::GetRenderer(uint64_t Id)
		{
			for (auto& RenderStage : Renderers)
			{
				if (RenderStage->GetId() == Id)
					return RenderStage;
			}

			return nullptr;
		}
		Core::Pool<Component*>* RenderSystem::GetSceneComponents(uint64_t Section)
		{
			return Scene->GetComponents(Section);
		}
		size_t RenderSystem::GetDepthSize()
		{
			return DepthSize;
		}
		std::vector<Renderer*>* RenderSystem::GetRenderers()
		{
			return &Renderers;
		}
		Graphics::MultiRenderTarget2D* RenderSystem::GetMRT(TargetType Type)
		{
			return Scene->GetMRT(Type);
		}
		Graphics::RenderTarget2D* RenderSystem::GetRT(TargetType Type)
		{
			return Scene->GetRT(Type);
		}
		Graphics::GraphicsDevice* RenderSystem::GetDevice()
		{
			return Device;
		}
		Graphics::Texture2D** RenderSystem::GetMerger()
		{
			return Scene->GetMerger();
		}
		PrimitiveCache* RenderSystem::GetPrimitives()
		{
			return Scene->GetPrimitives();
		}
		SceneGraph* RenderSystem::GetScene()
		{
			return Scene;
		}

		GeometryDraw::GeometryDraw(RenderSystem* Lab, uint64_t Hash) : Renderer(Lab), Source(Hash)
		{
		}
		GeometryDraw::~GeometryDraw()
		{
		}
		void GeometryDraw::CullGeometry(const Viewer& View, Core::Pool<Drawable*>* Geometry)
		{
		}
		void GeometryDraw::CullGeometry(const Viewer& View)
		{
			Core::Pool<Drawable*>* Opaque = GetOpaque();
			if (Opaque != nullptr && Opaque->Size() > 0)
				CullGeometry(View, Opaque);
		}
		void GeometryDraw::Render(Core::Timer* TimeStep, RenderState State, RenderOpt Options)
		{
			if (State == RenderState::Geometry_Result)
			{
				Core::Pool<Drawable*>* Geometry;
				if ((size_t)Options & (size_t)RenderOpt::Transparent)
					Geometry = GetTransparent();
				else if ((size_t)Options & (size_t)RenderOpt::Additive)
					Geometry = GetAdditive();
				else
					Geometry = GetOpaque();

				TH_PPUSH("render-result", TH_PERF_CORE);
				if (Geometry != nullptr && Geometry->Size() > 0)
				{
					System->ClearMaterials();
					RenderGeometryResult(TimeStep, Geometry, Options);
				}
				TH_PPOP();
			}
			else if (State == RenderState::Geometry_Voxels)
			{
				if ((size_t)Options & (size_t)RenderOpt::Transparent || (size_t)Options & (size_t)RenderOpt::Additive)
					return;

				TH_PPUSH("render-voxels", TH_PERF_MIX);
				Core::Pool<Drawable*>* Geometry = GetOpaque();
				if (Geometry != nullptr && Geometry->Size() > 0)
				{
					System->ClearMaterials();
					RenderGeometryVoxels(TimeStep, Geometry, Options);
				}
				TH_PPOP();
			}
			else if (State == RenderState::Depth_Linear)
			{
				if (!((size_t)Options & (size_t)RenderOpt::Inner))
					return;

				TH_PPUSH("render-depth-linear", TH_PERF_FRAME);
				System->ClearMaterials();

				Core::Pool<Drawable*>* Opaque = GetOpaque();
				if (Opaque != nullptr && Opaque->Size() > 0)
					RenderDepthLinear(TimeStep, Opaque);

				Core::Pool<Drawable*>* Transparent = GetTransparent();
				if (Transparent != nullptr && Transparent->Size() > 0)
					RenderDepthLinear(TimeStep, Transparent);
				TH_PPOP();
			}
			else if (State == RenderState::Depth_Cubic)
			{
				if (!((size_t)Options & (size_t)RenderOpt::Inner))
					return;

				TH_PPUSH("render-depth-cubic", TH_PERF_FRAME);
				System->ClearMaterials();

				Core::Pool<Drawable*>* Opaque = GetOpaque();
				if (Opaque != nullptr && Opaque->Size() > 0)
					RenderDepthCubic(TimeStep, Opaque, System->View.CubicViewProjection);

				Core::Pool<Drawable*>* Transparent = GetTransparent();
				if (Transparent != nullptr && Transparent->Size() > 0)
					RenderDepthCubic(TimeStep, Transparent, System->View.CubicViewProjection);
				TH_PPOP();
			}
		}
		Core::Pool<Drawable*>* GeometryDraw::GetOpaque()
		{
			return System->GetScene()->GetOpaque(Source);
		}
		Core::Pool<Drawable*>* GeometryDraw::GetTransparent()
		{
			return System->GetScene()->GetTransparent(Source);
		}
		Core::Pool<Drawable*>* GeometryDraw::GetAdditive()
		{
			return System->GetScene()->GetAdditive(Source);
		}

		EffectDraw::EffectDraw(RenderSystem* Lab) : Renderer(Lab), Output(nullptr), Swap(nullptr), MaxSlot(0)
		{
			TH_ASSERT_V(Lab != nullptr, "render system should be set");
			TH_ASSERT_V(Lab->GetDevice() != nullptr, "graphics device should be set");

			auto* Device = Lab->GetDevice();
			DepthStencil = Device->GetDepthStencilState("none");
			Rasterizer = Device->GetRasterizerState("cull-back");
			Blend = Device->GetBlendState("overwrite-opaque");
			Sampler = Device->GetSamplerState("trilinear-x16");
			Layout = Device->GetInputLayout("shape-vertex");
		}
		EffectDraw::~EffectDraw()
		{
			for (auto It = Effects.begin(); It != Effects.end(); ++It)
				System->FreeShader(It->first, It->second);
		}
		void EffectDraw::ResizeBuffers()
		{
			Output = nullptr;
			ResizeEffect();
		}
		void EffectDraw::ResizeEffect()
		{
		}
		void EffectDraw::RenderOutput(Graphics::RenderTarget2D* Resource)
		{
			TH_ASSERT_V(System->GetDevice() != nullptr, "graphics device should be set");
			if (Resource != nullptr)
			{
				Output = Resource;
				Swap = Resource;
			}
			else
				Output = System->GetRT(TargetType::Main);

			Graphics::GraphicsDevice* Device = System->GetDevice();
			Device->SetTarget(Output, 0, 0, 0, 0);
		}
		void EffectDraw::RenderTexture(uint32_t Slot6, Graphics::Texture2D* Resource)
		{
			Graphics::GraphicsDevice* Device = System->GetDevice();
			Device->SetTexture2D(Resource, 6 + Slot6, TH_PS);

			if (Resource != nullptr)
				MaxSlot = std::max(MaxSlot, 6 + Slot6);
		}
		void EffectDraw::RenderTexture(uint32_t Slot6, Graphics::Texture3D* Resource)
		{
			Graphics::GraphicsDevice* Device = System->GetDevice();
			Device->SetTexture3D(Resource, 6 + Slot6, TH_PS);

			if (Resource != nullptr)
				MaxSlot = std::max(MaxSlot, 6 + Slot6);
		}
		void EffectDraw::RenderTexture(uint32_t Slot6, Graphics::TextureCube* Resource)
		{
			Graphics::GraphicsDevice* Device = System->GetDevice();
			Device->SetTextureCube(Resource, 6 + Slot6, TH_PS);

			if (Resource != nullptr)
				MaxSlot = std::max(MaxSlot, 6 + Slot6);
		}
		void EffectDraw::RenderMerge(Graphics::Shader* Effect, void* Buffer, size_t Count)
		{
			TH_ASSERT_V(Count > 0, "count should be greater than zero");
			if (!Effect)
				Effect = Effects.begin()->second;

			Graphics::GraphicsDevice* Device = System->GetDevice();
			Graphics::Texture2D** Merger = System->GetMerger();

			if (Swap != nullptr && Output != Swap)
				Device->SetTexture2D(Swap->GetTarget(), 5, TH_PS);
			else if (Merger != nullptr)
				Device->SetTexture2D(*Merger, 5, TH_PS);

			Device->SetShader(Effect, TH_VS | TH_PS);
			if (Buffer != nullptr)
			{
				Device->UpdateBuffer(Effect, Buffer);
				Device->SetBuffer(Effect, 3, TH_VS | TH_PS);
			}

			for (size_t i = 0; i < Count; i++)
			{
				Device->Draw(6, 0);
				if (!Swap)
					Device->CopyTexture2D(Output, 0, Merger);
			}

			if (Swap == Output)
				RenderOutput();
		}
		void EffectDraw::RenderResult(Graphics::Shader* Effect, void* Buffer)
		{
			if (!Effect)
				Effect = Effects.begin()->second;

			Graphics::GraphicsDevice* Device = System->GetDevice();
			Graphics::Texture2D** Merger = System->GetMerger();

			if (Swap != nullptr && Output != Swap)
				Device->SetTexture2D(Swap->GetTarget(), 5, TH_PS);
			else if (Merger != nullptr)
				Device->SetTexture2D(*Merger, 5, TH_PS);

			Device->SetShader(Effect, TH_VS | TH_PS);
			if (Buffer != nullptr)
			{
				Device->UpdateBuffer(Effect, Buffer);
				Device->SetBuffer(Effect, 3, TH_VS | TH_PS);
			}

			Device->Draw(6, 0);
			Output = System->GetRT(TargetType::Main);
		}
		void EffectDraw::RenderEffect(Core::Timer* Time)
		{
		}
		void EffectDraw::Render(Core::Timer* Time, RenderState State, RenderOpt Options)
		{
			TH_ASSERT_V(System->GetPrimitives() != nullptr, "primitive cache should be set");
			TH_ASSERT_V(System->GetMRT(TargetType::Main) != nullptr, "main render target should be set");

			if (State != RenderState::Geometry_Result || (size_t)Options & (size_t)RenderOpt::Inner)
				return;

			MaxSlot = 5;
			if (Effects.empty())
				return;

			Swap = nullptr;
			if (!Output)
				Output = System->GetRT(TargetType::Main);

			TH_PPUSH("render-effect", TH_PERF_FRAME);
			Graphics::MultiRenderTarget2D* Input = System->GetMRT(TargetType::Main);
			PrimitiveCache* Cache = System->GetPrimitives();
			Graphics::GraphicsDevice* Device = System->GetDevice();
			Device->SetDepthStencilState(DepthStencil);
			Device->SetBlendState(Blend);
			Device->SetRasterizerState(Rasterizer);
			Device->SetInputLayout(Layout);
			Device->SetTarget(Output, 0, 0, 0, 0);
			Device->SetSamplerState(Sampler, 1, MaxSlot, TH_PS);
			Device->SetTexture2D(Input->GetTarget(0), 1, TH_PS);
			Device->SetTexture2D(Input->GetTarget(1), 2, TH_PS);
			Device->SetTexture2D(Input->GetTarget(2), 3, TH_PS);
			Device->SetTexture2D(Input->GetTarget(3), 4, TH_PS);
			Device->SetVertexBuffer(Cache->GetQuad(), 0);

			RenderEffect(Time);

			Device->FlushTexture(1, MaxSlot, TH_PS);
			Device->CopyTarget(Output, 0, Input, 0);
			System->RestoreOutput();
			TH_PPOP();
		}
		Graphics::Shader* EffectDraw::GetEffect(const std::string& Name)
		{
			auto It = Effects.find(Name);
			if (It != Effects.end())
				return It->second;

			return nullptr;
		}
		Graphics::Shader* EffectDraw::CompileEffect(Graphics::Shader::Desc& Desc, size_t BufferSize)
		{
			TH_ASSERT(!Desc.Filename.empty(), nullptr, "cannot compile unnamed shader source");
			Graphics::Shader* Shader = System->CompileShader(Desc, BufferSize);
			if (!Shader)
				return nullptr;

			auto It = Effects.find(Desc.Filename);
			if (It != Effects.end())
			{
				TH_RELEASE(It->second);
				It->second = Shader;
			}
			else
				Effects[Desc.Filename] = Shader;

			return Shader;
		}
		Graphics::Shader* EffectDraw::CompileEffect(const std::string& SectionName, size_t BufferSize)
		{
			Graphics::Shader::Desc I = Graphics::Shader::Desc();
			if (!System->GetDevice()->GetSection(SectionName, &I))
				return nullptr;

			return CompileEffect(I, BufferSize);
		}
		unsigned int EffectDraw::GetMipLevels()
		{
			TH_ASSERT(System->GetRT(TargetType::Main) != nullptr, 0, "main render target should be set");
			Graphics::RenderTarget2D* RT = System->GetRT(TargetType::Main);
			return System->GetDevice()->GetMipLevel(RT->GetWidth(), RT->GetHeight());
		}
		unsigned int EffectDraw::GetWidth()
		{
			TH_ASSERT(System->GetRT(TargetType::Main) != nullptr, 0, "main render target should be set");
			Graphics::RenderTarget2D* RT = System->GetRT(TargetType::Main);
			return RT->GetWidth();
		}
		unsigned int EffectDraw::GetHeight()
		{
			TH_ASSERT(System->GetRT(TargetType::Main) != nullptr, 0, "main render target should be set");
			Graphics::RenderTarget2D* RT = System->GetRT(TargetType::Main);
			return RT->GetHeight();
		}

		SceneGraph::Desc SceneGraph::Desc::Get(Application* Base)
		{
			SceneGraph::Desc I;
			if (Base != nullptr)
			{
				I.MinFrames = Base->Control.MinFrames;
				I.MaxFrames = Base->Control.MaxFrames;
				I.Async = Base->Control.Async;
				I.Shaders = Base->Cache.Shaders;
				I.Primitives = Base->Cache.Primitives;
				I.Device = Base->Renderer;
				I.Manager = Base->VM;
			}

			return I;
		}

		SceneGraph::SceneGraph(const Desc& I) : Simulator(new Compute::Simulator(I.Simulator)), Camera(nullptr), Conf(I), Surfaces(16), Status(-1), Acquire(false), Active(true), Snapshot(nullptr)
		{
			for (size_t i = 0; i < (size_t)TargetType::Count * 2; i++)
			{
				Display.MRT[i] = nullptr;
				Display.RT[i] = nullptr;
			}

			Display.VoxelBuffers[(size_t)VoxelType::Diffuse] = nullptr;
			Display.VoxelBuffers[(size_t)VoxelType::Normal] = nullptr;
			Display.VoxelBuffers[(size_t)VoxelType::Surface] = nullptr;
			Display.MaterialBuffer = nullptr;
			Display.Merger = nullptr;
			Display.DepthStencil = nullptr;
			Display.Rasterizer = nullptr;
			Display.Blend = nullptr;
			Display.Sampler = nullptr;
			Display.Layout = nullptr;
			Display.VoxelSize = 0;

			Configure(I);
			ScriptHook();
			ExpandMaterials();
			SetParallel("simulate", std::bind(&SceneGraph::Simulate, this, std::placeholders::_1));
			SetParallel("synchronize", std::bind(&SceneGraph::Synchronize, this, std::placeholders::_1));
			for (uint32_t i = 0; i < I.CullingChunks; i++)
				SetParallel("cull-" + std::to_string(i), std::bind(&SceneGraph::Cullout, this, std::placeholders::_1, i));
			Status = 0;
		}
		SceneGraph::~SceneGraph()
		{
			TH_PPUSH("scene-destroy", TH_PERF_MAX);
			auto* Schedule = Core::Schedule::Get();
			auto Source = std::move(Listeners);
			for (auto& Item : Source)
			{
				for (auto* Listener : Item.second)
					TH_DELETE(function, Listener);
			}

			Status = -1;
			for (auto It = Tasks.begin(); It != Tasks.end(); It++)
			{
				while (It->second->Active)
					Schedule->Dispatch();

				TH_RELEASE(It->second->Time);
				TH_DELETE(Packet, It->second);
			}

			Tasks.clear();
			while (Dispatch(nullptr))
				TH_OSIG();

			auto Begin1 = Entities.Begin(), End1 = Entities.End();
			for (auto It = Begin1; It != End1; ++It)
				TH_RELEASE(*It);

			auto Begin2 = Materials.Begin(), End2 = Materials.End();
			for (auto It = Begin2; It != End2; ++It)
				TH_RELEASE(*It);

			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Diffuse]);
			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Normal]);
			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Surface]);
			TH_RELEASE(Display.Merger);

			for (size_t i = 0; i < (size_t)TargetType::Count; i++)
			{
				TH_RELEASE(Display.MRT[i]);
				TH_RELEASE(Display.RT[i]);
			}

			TH_RELEASE(Display.MaterialBuffer);
			TH_RELEASE(Simulator);
			TH_PPOP();
		}
		void SceneGraph::Configure(const Desc& NewConf)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Exclusive([this, NewConf]()
			{
				Conf = NewConf;
				Display.DepthStencil = Conf.Device->GetDepthStencilState("none");
				Display.Rasterizer = Conf.Device->GetRasterizerState("cull-back");
				Display.Blend = Conf.Device->GetBlendState("overwrite");
				Display.Sampler = Conf.Device->GetSamplerState("trilinear-x16");
				Display.Layout = Conf.Device->GetInputLayout("shape-vertex");
				Materials.Reserve(Conf.MaterialCount);
				Entities.Reserve(Conf.EntityCount);

				for (size_t i = 0; i < (size_t)ActorType::Count; i++)
					Actors[i].Reserve(Conf.ComponentCount);

				for (auto& Array : Components)
					Array.second.Reserve(Conf.ComponentCount);

				for (auto& Array : Drawables)
				{
					Array.second.Opaque.Reserve(Conf.ComponentCount);
					Array.second.Transparent.Reserve(Conf.ComponentCount);
					Array.second.Additive.Reserve(Conf.ComponentCount);
				}

				SetTiming(Conf.MinFrames, Conf.MaxFrames);
				ResizeBuffers();

				auto* Viewer = Camera.load();
				if (Viewer != nullptr)
					Viewer->Activate(Viewer);

				Core::Pool<Component*>* Cameras = GetComponents<Components::Camera>();
				if (!Cameras)
					return;

				for (auto It = Cameras->Begin(); It != Cameras->End(); ++It)
				{
					auto* Base = (Components::Camera*)(*It);
					Base->GetRenderer()->Remount();
				}
			});
		}
		void SceneGraph::ExclusiveLock()
		{
			if (!Conf.Async)
				return;

			if (Status != 1)
				return Emulation.lock();

			TH_ASSERT_V(Status == 0 || ThreadId == std::this_thread::get_id(), "exclusive lock should be called in same thread with dispatch");
			TH_ASSERT_V(!Acquire, "exclusive lock should not be acquired");

			Acquire = true;
			while (IsUnstable())
			{
				std::unique_lock<std::mutex> Lock(Race);
				Stabilize.wait(Lock);
			}
		}
		void SceneGraph::ExclusiveUnlock()
		{
			if (!Conf.Async)
				return;

			if (Status != 1)
				return Emulation.unlock();

			TH_ASSERT_V(Status == 0 || ThreadId == std::this_thread::get_id(), "exclusive unlock should be called in same thread with dispatch");
			TH_ASSERT_V(Acquire, "exclusive lock should be acquired");

			Acquire = false;
			Race.lock();
			ExecuteTasks();
			Race.unlock();
		}
		void SceneGraph::ExpandMaterials()
		{
			Exclusive([this]()
			{
				Graphics::ElementBuffer::Desc F = Graphics::ElementBuffer::Desc();
				F.AccessFlags = Graphics::CPUAccess::Write;
				F.MiscFlags = Graphics::ResourceMisc::Buffer_Structured;
				F.Usage = Graphics::ResourceUsage::Dynamic;
				F.BindFlags = Graphics::ResourceBind::Shader_Input;
				F.ElementCount = (unsigned int)Surfaces;
				F.ElementWidth = sizeof(Subsurface);
				F.StructureByteStride = F.ElementWidth;

				TH_RELEASE(Display.MaterialBuffer);
				Display.MaterialBuffer = Conf.Device->CreateElementBuffer(F);
				Surfaces = Surfaces * 2;
			});
		}
		void SceneGraph::ResizeBuffers()
		{
			Exclusive([this]()
			{
				ResizeRenderBuffers();
				if (!Camera.load())
					return;

				auto* Array = GetComponents<Components::Camera>();
				for (auto It = Array->Begin(); It != Array->End(); ++It)
					((Components::Camera*)*It)->ResizeBuffers();
			});
		}
		void SceneGraph::ResizeRenderBuffers()
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::MultiRenderTarget2D::Desc MRT = GetDescMRT();
			Graphics::RenderTarget2D::Desc RT = GetDescRT();
			TH_CLEAR(Display.Merger);

			for (size_t i = 0; i < (size_t)TargetType::Count; i++)
			{
				TH_RELEASE(Display.MRT[i]);
				Display.MRT[i] = Conf.Device->CreateMultiRenderTarget2D(MRT);

				TH_RELEASE(Display.RT[i]);
				Display.RT[i] = Conf.Device->CreateRenderTarget2D(RT);
			}
		}
		void SceneGraph::FillMaterialBuffers()
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::MappedSubresource Stream;
			if (!Conf.Device->Map(Display.MaterialBuffer, Graphics::ResourceMap::Write_Discard, &Stream))
				return;

			Subsurface* Array = (Subsurface*)Stream.Pointer; uint64_t Size = 0;
			auto Begin = Materials.Begin(), End = Materials.End();
			for (auto It = Begin; It != End; ++It)
			{
				Subsurface& Next = Array[Size];
				(*It)->Slot = (int64_t)Size;
				Next = (*It)->Surface;

				if (++Size >= Surfaces)
					break;
			}

			Conf.Device->Unmap(Display.MaterialBuffer, &Stream);
			Conf.Device->SetStructureBuffer(Display.MaterialBuffer, 0, TH_PS | TH_CS);
		}
		void SceneGraph::Actualize()
		{
			Redistribute();
			Reindex();
		}
		void SceneGraph::Redistribute()
		{
			Exclusive([this]()
			{
				TH_PPUSH("scene-redist", TH_PERF_FRAME);
				for (auto& Component : Components)
					Component.second.Clear();

				for (size_t i = 0; i < (size_t)ActorType::Count; i++)
					Actors[i].Clear();

				auto Begin = Entities.Begin(), End = Entities.End();
				for (auto It = Begin; It != End; ++It)
					RegisterEntity(*It);

				GetCamera();
				TH_PPOP();
			});
		}
		void SceneGraph::Reindex()
		{
			Exclusive([this]()
			{
				TH_PPUSH("scene-index", TH_PERF_FRAME);
				int64_t Index = -1;
				auto Begin = Materials.Begin(), End = Materials.End();
				for (auto It = Begin; It != End; ++It)
					(*It)->Slot = ++Index;
				TH_PPOP();
			});
		}
		void SceneGraph::Sleep()
		{
			Status = 0;
			while (IsUnstable());
			Status = 1;
		}
		void SceneGraph::Submit()
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			TH_ASSERT_V(Conf.Primitives != nullptr, "graphics device should be set");
			TH_ASSERT_V(ThreadId == std::this_thread::get_id(), "submit should be called in same thread with publish (after)");

			Conf.Device->Render.TexCoord = 1.0f;
			Conf.Device->Render.WorldViewProj.Identify();
			Conf.Device->SetTarget();
			Conf.Device->SetDepthStencilState(Display.DepthStencil);
			Conf.Device->SetBlendState(Display.Blend);
			Conf.Device->SetRasterizerState(Display.Rasterizer);
			Conf.Device->SetInputLayout(Display.Layout);
			Conf.Device->SetSamplerState(Display.Sampler, 1, 1, TH_PS);
			Conf.Device->SetTexture2D(Display.MRT[(size_t)TargetType::Main]->GetTarget(0), 1, TH_PS);
			Conf.Device->SetShader(Conf.Device->GetBasicEffect(), TH_VS | TH_PS);
			Conf.Device->SetVertexBuffer(Conf.Primitives->GetQuad(), 0);
			Conf.Device->UpdateBuffer(Graphics::RenderBufferType::Render);
			Conf.Device->Draw(6, 0);
			Conf.Device->SetTexture2D(nullptr, 1, TH_PS);
		}
		void SceneGraph::Conform()
		{
#ifdef _DEBUG
			ThreadId = std::this_thread::get_id();
#endif
			auto* Schedule = Core::Schedule::Get();
			if (Status == 0 || Queue.empty())
				return;

			std::queue<Core::TaskCallback> Next;
			ExclusiveLock();
		Iterate:
			Race.lock();
			Next.swap(Queue);
			Race.unlock();

			while (!Next.empty())
			{
				Next.front()();
				Next.pop();
			}

			bool Process = !Events.empty();
			while (!Events.empty())
				ResolveEvents();

			if (Process)
				goto Iterate;

			ExclusiveUnlock();
		}
		bool SceneGraph::Dispatch(Core::Timer* Time)
		{
#ifdef _DEBUG
			ThreadId = std::this_thread::get_id();
#endif
			TH_PPUSH("scene-dispatch", TH_PERF_FRAME);
			bool Result = false;
			if (Status != 0)
			{
				if (ResolveEvents())
					goto Skip;

				if (Queue.empty())
					goto Update;

				std::queue<Core::TaskCallback> Next;
				Race.lock();
				Next.swap(Queue);
				Race.unlock();

				if (Next.empty())
					goto Update;

				ExclusiveLock();
				while (!Next.empty())
				{
					Next.front()();
					Next.pop();
				}
				ExclusiveUnlock();
			}
			else
			{
				Status = 1;
				Race.lock();
				ExecuteTasks();
				Race.unlock();
			}
		Skip:
			Result = true;
		Update:
			if (!Active || !Time)
				TH_PRET(Result);

			auto Begin = Actors[(size_t)ActorType::Update].Begin();
			auto End = Actors[(size_t)ActorType::Update].End();
			for (auto It = Begin; It != End; ++It)
				(*It)->Update(Time);

			TH_PRET(Result);
		}
		void SceneGraph::Publish(Core::Timer* Time)
		{
			TH_ASSERT_V(Time != nullptr, "timer should be set");
			TH_ASSERT_V(ThreadId == std::this_thread::get_id(), "publish should be called in same thread with dispatch (after)");

			auto* Base = (Components::Camera*)Camera.load();
			if (!Base)
				return;

			auto* Renderer = Base->GetRenderer();
			TH_ASSERT_V(Renderer != nullptr, "render system should be set");

			FillMaterialBuffers();
			Renderer->RestoreViewBuffer(nullptr);
			Renderer->CullGeometry(Time);

			SetMRT(TargetType::Main, true);
			Renderer->Render(Time, RenderState::Geometry_Result, RenderOpt::None);
		}
		void SceneGraph::Simulate(Core::Timer* Time)
		{
			TH_ASSERT_V(Time != nullptr, "timer should be set");
			TH_ASSERT_V(Simulator != nullptr, "simulator should be set");
			TH_PPUSH("scene-sim", TH_PERF_CORE);
			if (Active)
				Simulator->Simulate((float)Time->GetTimeStep());
			TH_PPOP();
		}
		void SceneGraph::Synchronize(Core::Timer* Time)
		{
			TH_PPUSH("scene-sync", TH_PERF_CORE);
			auto Begin1 = Actors[(size_t)ActorType::Synchronize].Begin();
			auto End1 = Actors[(size_t)ActorType::Synchronize].End();
			for (auto It = Begin1; It != End1; ++It)
				(*It)->Synchronize(Time);

			Component* Base = Camera.load();
			if (Base != nullptr)
			{
				int64_t Index = -1; bool Dirty = Base->Parent->Transform->IsDirty();
				Compute::Vector3 Far = Base->Parent->Transform->GetPosition();
				auto Begin2 = Entities.Begin(), End2 = Entities.End();
				for (auto It = Begin2; It != End2; ++It)
				{
					Entity* Base = *It;
					if (Base->Transform->IsDirty())
					{
						Base->Transform->Synchronize();
						Base->Dirty = true;
					}
					else if (!Dirty)
						continue;

					Base->Distance = Base->Transform->GetPosition().Distance(Far);
				}
			}
			TH_PPOP();
		}
		void SceneGraph::Cullout(Core::Timer* Time, uint32_t Chunk)
		{
			TH_PPUSH("scene-cull", TH_PERF_CORE);
			auto* Base = (Components::Camera*)Camera.load();
			if (Base != nullptr)
			{
				auto* Renderer = Base->GetRenderer();
				if (Renderer != nullptr)
				{
					double Where = (double)Chunk, Count = (double)Conf.CullingChunks;
					Renderer->Synchronize(Time, Where / Count, (Where + 1.0) / Count, Base->Parent->GetTransform()->IsDirty());
				}
			}
			TH_PPOP();
		}
		void SceneGraph::SortBackToFront(Core::Pool<Drawable*>* Array)
		{
			TH_ASSERT_V(Array != nullptr, "array should be set");
			TH_PPUSH("sort-btf", TH_PERF_FRAME);

			std::sort(Array->Begin(), Array->End(), [](Component* A, Component* B)
			{
				return A->Parent->Distance > B->Parent->Distance;
			});

			TH_PPOP();
		}
		void SceneGraph::SortFrontToBack(Core::Pool<Drawable*>* Array)
		{
			TH_ASSERT_V(Array != nullptr, "array should be set");
			TH_PPUSH("sort-ftb", TH_PERF_FRAME);

			std::sort(Array->Begin(), Array->End(), [](Component* A, Component* B)
			{
				return A->Parent->Distance < B->Parent->Distance;
			});

			TH_PPOP();
		}
		void SceneGraph::SetCamera(Entity* NewCamera)
		{
			if (!NewCamera)
			{
				Camera = nullptr;
				return;
			}

			Components::Camera* Target = NewCamera->GetComponent<Components::Camera>();
			if (!Target || !Target->Active)
			{
				Camera = nullptr;
				return;
			}

			Component* Viewer = Camera.load();
			if (Viewer != nullptr)
				Viewer->Activate(Viewer);
			Target->Activate(nullptr);
			Camera = Target;
		}
		void SceneGraph::RemoveEntity(Entity* Entity, bool Release)
		{
			TH_ASSERT_V(Entity != nullptr, "entity should be set");
			if (!UnregisterEntity(Entity) || !Release)
				return;

			Entity->RemoveChilds();
			Exclusive([Entity]()
			{
				TH_RELEASE(Entity);
			});
		}
		void SceneGraph::RemoveMaterial(Material* Value)
		{
			TH_ASSERT_V(Value != nullptr, "entity should be set");
			Mutate(Value, "pop");
			Exclusive([this, Value]()
			{
				auto Begin = Materials.Begin(), End = Materials.End();
				for (auto It = Begin; It != End; ++It)
				{
					if (*It == Value)
					{
						TH_RELEASE(Value);
						Materials.RemoveAt(It);
						break;
					}
				}
			});
		}
		void SceneGraph::RegisterEntity(Entity* In)
		{
			TH_ASSERT_V(In != nullptr, "entity should be set");
			for (auto& Component : In->Components)
			{
				Component.second->Activate(Component.second);
				auto* Storage = GetComponents(Component.second->GetId());
				if (Component.second->Active)
				{
					if (In->Active)
					{
						Storage->AddIfNotExists(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Update)
							Actors[(size_t)ActorType::Update].AddIfNotExists(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Synchronize)
							Actors[(size_t)ActorType::Synchronize].AddIfNotExists(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Message)
							Actors[(size_t)ActorType::Message].AddIfNotExists(Component.second);
					}
					else
					{
						Storage->Add(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Update)
							Actors[(size_t)ActorType::Update].Add(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Synchronize)
							Actors[(size_t)ActorType::Synchronize].Add(Component.second);
						if (Component.second->Set & (size_t)ActorSet::Message)
							Actors[(size_t)ActorType::Message].Add(Component.second);
					}
					Mutate(Component.second, "push");
				}
				else
				{
					Storage->Remove(Component.second);
					if (Component.second->Set & (size_t)ActorSet::Update)
						Actors[(size_t)ActorType::Update].Remove(Component.second);
					if (Component.second->Set & (size_t)ActorSet::Synchronize)
						Actors[(size_t)ActorType::Synchronize].Remove(Component.second);
					if (Component.second->Set & (size_t)ActorSet::Message)
						Actors[(size_t)ActorType::Message].Remove(Component.second);
					Mutate(Component.second, "pop");
				}
			}

			In->Active = true;
			Mutate(In, "push");
		}
		bool SceneGraph::UnregisterEntity(Entity* In)
		{
			TH_ASSERT(In != nullptr, false, "entity should be set");
			TH_ASSERT(In->GetScene() == this, false, "entity should be attached to current scene");

			Component* Viewer = Camera.load();
			if (Viewer != nullptr && In == Viewer->Parent)
				Camera = nullptr;

			for (auto& Component : In->Components)
			{
				Component.second->Deactivate();
				auto* Storage = &Components[Component.second->GetId()];
				Storage->Remove(Component.second);
				if (Component.second->Set & (size_t)ActorSet::Update)
					Actors[(size_t)ActorType::Update].Remove(Component.second);
				if (Component.second->Set & (size_t)ActorSet::Synchronize)
					Actors[(size_t)ActorType::Synchronize].Remove(Component.second);
				if (Component.second->Set & (size_t)ActorSet::Message)
					Actors[(size_t)ActorType::Message].Remove(Component.second);
				Mutate(Component.second, "pop");
			}

			In->Active = false;
			Entities.Remove(In);
			Mutate(In, "pop");
			return true;
		}
		void SceneGraph::CloneEntities(Entity* Instance, std::vector<Entity*>* Array)
		{
			TH_ASSERT_V(Instance != nullptr, "entity should be set");
			TH_ASSERT_V(Array != nullptr, "array should be set");

			Entity* Clone = CloneEntity(Instance);
			Array->push_back(Clone);

			Compute::Transform* Root = Clone->Transform->GetRoot();
			if (Root != nullptr)
				Root->GetChilds().push_back(Clone->Transform);

			if (!Instance->Transform->GetChildsCount())
				return;

			std::vector<Compute::Transform*>& Childs = Instance->Transform->GetChilds();
			Clone->Transform->GetChilds().clear();

			for (auto& Child : Childs)
			{
				uint64_t Offset = Array->size();
				CloneEntities(Child->Ptr<Entity>(), Array);
				for (uint64_t j = Offset; j < Array->size(); j++)
				{
					if ((*Array)[j]->Transform->GetRoot() == Instance->Transform)
						(*Array)[j]->Transform->SetRoot(Clone->Transform);
				}
			}
		}
		void SceneGraph::ExecuteTasks()
		{
			auto* Schedule = Core::Schedule::Get();
			for (auto It = Tasks.begin(); It != Tasks.end(); It++)
			{
				if (It->second->Active)
					continue;

				It->second->Active = true;
				if (!Schedule->SetTask(It->second->Callback))
					It->second->Active = false;
			}
		}
		void SceneGraph::RayTest(uint64_t Section, const Compute::Ray& Origin, float MaxDistance, const RayCallback& Callback)
		{
			TH_ASSERT_V(Callback, "callback should not be empty");
			TH_PPUSH("ray-test", TH_PERF_FRAME);

			Core::Pool<Component*>* Array = GetComponents(Section);
			Compute::Ray Base = Origin;
			Compute::Vector3 Hit;

			for (auto It = Array->Begin(); It != Array->End(); ++It)
			{
				Component* Current = *It;
				if (MaxDistance > 0.0f && Current->Parent->Distance > MaxDistance)
					continue;

				if (Compute::Common::CursorRayTest(Base, Current->GetBoundingBox(), &Hit) && !Callback(Current, Hit))
					break;
			}

			TH_PPOP();
		}
		void SceneGraph::ScriptHook(const std::string& Name)
		{
			auto* Array = GetComponents<Components::Scriptable>();
			for (auto It = Array->Begin(); It != Array->End(); ++It)
			{
				Components::Scriptable* Base = (Components::Scriptable*)*It;
				Base->CallEntry(Name);
			}
		}
		void SceneGraph::SetActive(bool Enabled)
		{
			Active = Enabled;
			if (!Active)
				return;

			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				Entity* V = *It;
				for (auto& Base : V->Components)
				{
					if (Base.second->Active)
						Base.second->Activate(nullptr);
				}
			}
		}
		void SceneGraph::SetTiming(double Min, double Max)
		{
			Race.lock();
			for (auto It = Tasks.begin(); It != Tasks.end(); It++)
				It->second->Time->SetStepLimitation(Min, Max);
			Race.unlock();
		}
		void SceneGraph::SetVoxelBufferSize(size_t Size)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			if (Size % 8 != 0)
				Size = Display.VoxelSize;

			Graphics::Texture3D::Desc I;
			I.Width = I.Height = I.Depth = Display.VoxelSize = Size;
			I.MipLevels = 0;
			I.Writable = true;

			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Diffuse]);
			Display.VoxelBuffers[(size_t)VoxelType::Diffuse] = Conf.Device->CreateTexture3D(I);

			I.FormatMode = Graphics::Format::R16G16B16A16_Float;
			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Normal]);
			Display.VoxelBuffers[(size_t)VoxelType::Normal] = Conf.Device->CreateTexture3D(I);

			I.FormatMode = Graphics::Format::R8G8B8A8_Unorm;
			TH_RELEASE(Display.VoxelBuffers[(size_t)VoxelType::Surface]);
			Display.VoxelBuffers[(size_t)VoxelType::Surface] = Conf.Device->CreateTexture3D(I);
		}
		void SceneGraph::SetMRT(TargetType Type, bool Clear)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::MultiRenderTarget2D* Target = Display.MRT[(size_t)Type];
			Conf.Device->SetTarget(Target);

			if (!Clear)
				return;

			Conf.Device->Clear(Target, 0, 0, 0, 0);
			Conf.Device->Clear(Target, 1, 0, 0, 0);
			Conf.Device->Clear(Target, 2, 1, 0, 0);
			Conf.Device->Clear(Target, 3, 0, 0, 0);
			Conf.Device->ClearDepth(Target);
		}
		void SceneGraph::SetRT(TargetType Type, bool Clear)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::RenderTarget2D* Target = Display.RT[(size_t)Type];
			Conf.Device->SetTarget(Target);

			if (!Clear)
				return;

			Conf.Device->Clear(Target, 0, 0, 0, 0);
			Conf.Device->ClearDepth(Target);
		}
		void SceneGraph::SwapMRT(TargetType Type, Graphics::MultiRenderTarget2D* New)
		{
			size_t Index = (size_t)Type;
			if (Display.MRT[Index] == New)
				return;

			Graphics::MultiRenderTarget2D* Cache = Display.MRT[Index + (size_t)TargetType::Count];
			if (New != nullptr)
			{
				Graphics::MultiRenderTarget2D* Base = Display.MRT[Index];
				Display.MRT[Index] = New;

				if (!Cache)
					Display.MRT[Index + (size_t)TargetType::Count] = Base;
			}
			else if (Cache != nullptr)
			{
				Display.MRT[Index] = Cache;
				Display.MRT[Index + (size_t)TargetType::Count] = nullptr;
			}
		}
		void SceneGraph::SwapRT(TargetType Type, Graphics::RenderTarget2D* New)
		{
			size_t Index = (size_t)Type;
			Graphics::RenderTarget2D* Cache = Display.RT[Index + (size_t)TargetType::Count];
			if (New != nullptr)
			{
				Graphics::RenderTarget2D* Base = Display.RT[Index];
				Display.RT[Index] = New;

				if (!Cache)
					Display.RT[Index + (size_t)TargetType::Count] = Base;
			}
			else if (Cache != nullptr)
			{
				Display.RT[Index] = Cache;
				Display.RT[Index + (size_t)TargetType::Count] = nullptr;
			}
		}
		void SceneGraph::ClearMRT(TargetType Type, bool Color, bool Depth)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::MultiRenderTarget2D* Target = Display.MRT[(size_t)Type];
			if (Color)
			{
				Conf.Device->Clear(Target, 0, 0, 0, 0);
				Conf.Device->Clear(Target, 1, 0, 0, 0);
				Conf.Device->Clear(Target, 2, 1, 0, 0);
				Conf.Device->Clear(Target, 3, 0, 0, 0);
			}

			if (Depth)
				Conf.Device->ClearDepth(Target);
		}
		void SceneGraph::ClearRT(TargetType Type, bool Color, bool Depth)
		{
			TH_ASSERT_V(Conf.Device != nullptr, "graphics device should be set");
			Graphics::RenderTarget2D* Target = Display.RT[(size_t)Type];
			if (Color)
				Conf.Device->Clear(Target, 0, 0, 0, 0);

			if (Depth)
				Conf.Device->ClearDepth(Target);
		}
		void SceneGraph::Exclusive(Core::TaskCallback&& Callback)
		{
			TH_ASSERT_V(Callback, "callback should not be empty");
			if (Status != 1 || Acquire)
				return Callback();

			Race.lock();
			Queue.emplace(std::move(Callback));
			Race.unlock();
		}
		bool SceneGraph::GetVoxelBuffer(Graphics::Texture3D** In, Graphics::Texture3D** Out)
		{
			TH_ASSERT(In && Out, false, "input and output should be set");
			TH_ASSERT(Display.VoxelBuffers[0] != nullptr, false, "first voxel buffer should be set");
			TH_ASSERT(Display.VoxelBuffers[1] != nullptr, false, "second voxel buffer should be set");
			TH_ASSERT(Display.VoxelBuffers[2] != nullptr, false, "third voxel buffer should be set");

			for (unsigned int i = 0; i < 3; i++)
			{
				In[i] = Display.VoxelBuffers[i];
				Out[i] = nullptr;
			}

			return true;
		}
		MessageCallback* SceneGraph::SetListener(const std::string& EventName, MessageCallback&& Callback)
		{
			MessageCallback* Id = TH_NEW(MessageCallback, std::move(Callback));
			Race.lock();
			auto& Source = Listeners[EventName];
			Source.insert(Id);
			Race.unlock();

			return Id;
		}
		bool SceneGraph::ClearListener(const std::string& EventName, MessageCallback* Id)
		{
			TH_ASSERT(!EventName.empty(), false, "event name should not be empty");
			TH_ASSERT(Id != nullptr, false, "callback id should be set");

			Race.lock();
			auto& Source = Listeners[EventName];
			size_t Last = Source.size();
			Source.erase(Id);
			size_t Next = Source.size();
			Race.unlock();

			if (Last == Next)
				return false;

			TH_DELETE(function, Id);
			return true;
		}
		bool SceneGraph::SetParallel(const std::string& Name, PacketCallback&& Callback)
		{
			ExclusiveLock();
			Race.lock();

			if (Callback)
			{
				auto It = Tasks.find(Name);
				Packet* Task = (It == Tasks.end() ? TH_NEW(Packet) : It->second);
				Task->Time = (Task->Time ? Task->Time : new Core::Timer());
				Task->Time->SetStepLimitation(Conf.MinFrames, Conf.MaxFrames);
				Task->Time->FrameLimit = (Core::Schedule::Get()->IsBlockable() ? Conf.FrequencyHZ : 0.0);
				Task->Active = false;

				if (It == Tasks.end())
					Tasks[Name] = Task;

				Task->Callback = [this, Task, Callback = std::move(Callback)]()
				{
					if (!Acquire && Status == 1)
					{
						Callback(Task->Time);
						Task->Time->Synchronize();
						if (Core::Schedule::Get()->SetTask(Task->Callback))
							return;
					}

					Task->Active = false;
					Stabilize.notify_one();
				};
			}
			else
			{
				auto It = Tasks.find(Name);
				if (It == Tasks.end())
				{
					Race.unlock();
					ExclusiveUnlock();
					return false;
				}

				TH_RELEASE(It->second->Time);
				TH_DELETE(Packet, It->second);
				Tasks.erase(It);
			}

			Race.unlock();
			ExclusiveUnlock();

			return true;
		}
		bool SceneGraph::SetEvent(const std::string& EventName, Core::VariantArgs&& Args, bool Propagate)
		{
			Event Next(EventName, std::move(Args));
			Next.Args["__vb"] = Core::Var::Integer((int64_t)(Propagate ? EventTarget::Scene : EventTarget::Listener));
			Next.Args["__vt"] = Core::Var::Pointer((void*)this);

			Race.lock();
			Events.push(std::move(Next));
			Race.unlock();

			return false;
		}
		bool SceneGraph::SetEvent(const std::string& EventName, Core::VariantArgs&& Args, Component* Target)
		{
			TH_ASSERT(Target != nullptr, false, "target should be set");
			Event Next(EventName, std::move(Args));
			Next.Args["__vb"] = Core::Var::Integer((int64_t)EventTarget::Component);
			Next.Args["__vt"] = Core::Var::Pointer((void*)Target);

			Race.lock();
			Events.push(std::move(Next));
			Race.unlock();

			return false;
		}
		bool SceneGraph::SetEvent(const std::string& EventName, Core::VariantArgs&& Args, Entity* Target)
		{
			TH_ASSERT(Target != nullptr, false, "target should be set");
			Event Next(EventName, std::move(Args));
			Next.Args["__vb"] = Core::Var::Integer((int64_t)EventTarget::Entity);
			Next.Args["__vt"] = Core::Var::Pointer((void*)Target);

			Race.lock();
			Events.push(std::move(Next));
			Race.unlock();

			return false;
		}
		bool SceneGraph::IsUnstable()
		{
			Race.lock();
			for (auto It = Tasks.begin(); It != Tasks.end(); It++)
			{
				if (It->second->Active)
				{
					Race.unlock();
					return true;
				}
			}

			Race.unlock();
			return false;
		}
		bool SceneGraph::IsLeftHanded()
		{
			return Conf.Device->IsLeftHanded();
		}
		void SceneGraph::Mutate(Entity* Parent, Entity* Child, const char* Type)
		{
			TH_ASSERT_V(Parent != nullptr, "parent should be set");
			TH_ASSERT_V(Child != nullptr, "child should be set");
			TH_ASSERT_V(Type != nullptr, "type should be set");
			if (!Conf.Mutations)
				return;

			Core::VariantArgs Args;
			Args["parent"] = Core::Var::Pointer((void*)Parent);
			Args["child"] = Core::Var::Pointer((void*)Child);
			Args["type"] = Core::Var::String(Type, strlen(Type));

			SetEvent("mutation", std::move(Args), false);
		}
		void SceneGraph::Mutate(Entity* Target, const char* Type)
		{
			TH_ASSERT_V(Target != nullptr, "target should be set");
			TH_ASSERT_V(Type != nullptr, "type should be set");
			if (!Conf.Mutations)
				return;

			Core::VariantArgs Args;
			Args["entity"] = Core::Var::Pointer((void*)Target);
			Args["type"] = Core::Var::String(Type, strlen(Type));

			SetEvent("mutation", std::move(Args), false);
		}
		void SceneGraph::Mutate(Component* Target, const char* Type)
		{
			TH_ASSERT_V(Target != nullptr, "target should be set");
			TH_ASSERT_V(Type != nullptr, "type should be set");
			if (!Conf.Mutations)
				return;

			Core::VariantArgs Args;
			Args["component"] = Core::Var::Pointer((void*)Target);
			Args["type"] = Core::Var::String(Type, strlen(Type));

			SetEvent("mutation", std::move(Args), false);
		}
		void SceneGraph::Mutate(Material* Target, const char* Type)
		{
			TH_ASSERT_V(Target != nullptr, "target should be set");
			TH_ASSERT_V(Type != nullptr, "type should be set");
			if (!Conf.Mutations)
				return;

			Core::VariantArgs Args;
			Args["material"] = Core::Var::Pointer((void*)Target);
			Args["type"] = Core::Var::String(Type, strlen(Type));

			SetEvent("mutation", std::move(Args), false);
		}
		void SceneGraph::CloneEntity(Entity* Value, CloneCallback&& Callback)
		{
			TH_ASSERT_V(Value != nullptr, "entity should be set");
			Exclusive([this, Value, Callback = std::move(Callback)]()
			{
				std::vector<Entity*> Array;
				CloneEntities(Value, &Array);
				if (Callback)
					Callback(this, !Array.empty() ? Array.front() : nullptr);
			});
		}
		void SceneGraph::MakeSnapshot(IdxSnapshot* Result)
		{
			TH_ASSERT_V(Result != nullptr, "shapshot result should be set");
			Result->To.clear();
			Result->From.clear();

			uint64_t Index = 0;
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				Result->To[*It] = Index;
				Result->From[Index] = *It;
				Index++;
			}
		}
		bool SceneGraph::ResolveEvents()
		{
			if (Events.empty())
				return false;

			std::queue<Event> Next;
			Race.lock();
			Next.swap(Events);
			Race.unlock();

			if (Next.empty())
				return false;

			while (!Next.empty())
			{
				Event Source = std::move(Next.front());
				Next.pop();

				auto _Bubble = Source.Args.find("__vb"), _Target = Source.Args.find("__vt");
				if (_Bubble == Source.Args.end() || _Target == Source.Args.end())
					continue;

				EventTarget Bubble = (EventTarget)_Bubble->second.GetInteger();
				void* Target = _Target->second.GetPointer();

				if (Bubble == EventTarget::Scene)
				{
					auto Begin = Actors[(size_t)ActorType::Message].Begin();
					auto End = Actors[(size_t)ActorType::Message].End();
					for (auto It = Begin; It != End; ++It)
						(*It)->Message(Source.Name, Source.Args);
				}
				else if (Bubble == EventTarget::Entity)
				{
					Entity* Base = (Entity*)Target;
					for (auto& Item : Base->Components)
						Item.second->Message(Source.Name, Source.Args);
				}
				else if (Bubble == EventTarget::Component)
				{
					Component* Base = (Component*)Target;
					Base->Message(Source.Name, Source.Args);
				}

				Race.lock();
				auto It = Listeners.find(Source.Name);
				if (It == Listeners.end() || It->second.empty())
				{
					Race.unlock();
					continue;
				}
				auto Copy = It->second;
				Race.unlock();

				for (auto* Callback : Copy)
					(*Callback)(Source.Name, Source.Args);
			}

			return !Events.empty();
		}
		void SceneGraph::AddDrawable(Drawable* Source, GeoCategory Category)
		{
			TH_ASSERT_V(Source != nullptr, "drawable should be set");
			if (Source->Parent->Active)
			{
				if (Category == GeoCategory::Opaque)
					GetOpaque(Source->Source)->AddIfNotExists(Source);
				else if (Category == GeoCategory::Transparent)
					GetTransparent(Source->Source)->AddIfNotExists(Source);
				else if (Category == GeoCategory::Additive)
					GetAdditive(Source->Source)->AddIfNotExists(Source);
			}
			else
			{
				if (Category == GeoCategory::Opaque)
					GetOpaque(Source->Source)->Add(Source);
				else if (Category == GeoCategory::Transparent)
					GetTransparent(Source->Source)->Add(Source);
				else if (Category == GeoCategory::Additive)
					GetAdditive(Source->Source)->Add(Source);
			}
		}
		void SceneGraph::RemoveDrawable(Drawable* Source, GeoCategory Category)
		{
			TH_ASSERT_V(Source != nullptr, "drawable should be set");
			if (!Source->Parent->Active)
				return;

			if (Category == GeoCategory::Opaque)
				GetOpaque(Source->Source)->Remove(Source);
			else if (Category == GeoCategory::Transparent)
				GetTransparent(Source->Source)->Remove(Source);
			else if (Category == GeoCategory::Additive)
				GetAdditive(Source->Source)->Remove(Source);
		}
		Material* SceneGraph::AddMaterial(Material* Base, const std::string& Name)
		{
			TH_ASSERT(Base != nullptr, nullptr, "base should be set");
			TH_ASSERT(Materials.Size() < Materials.Capacity(), nullptr, "too many materials");

			if (Materials.Size() > Surfaces)
				ExpandMaterials();

			Base->Scene = this;
			if (!Name.empty())
				Base->Name = Name;

			Materials.AddIfNotExists(Base);
			Mutate(Base, "push");

			return Base;
		}
		Material* SceneGraph::CloneMaterial(Material* Base, const std::string& Name)
		{
			TH_ASSERT(Base != nullptr, nullptr, "material should be set");
			return AddMaterial(new Material(*Base), Name);
		}
		Component* SceneGraph::GetCamera()
		{
			Component* Result = Camera.load();
			if (Result != nullptr)
				return Result;

			Result = GetComponent<Components::Camera>();
			if (!Result || !Result->Active)
			{
				Entity* Next = new Entity(this);
				Result = Next->AddComponent<Components::Camera>();
				AddEntity(Next);
				SetCamera(Next);
			}
			else
			{
				Result->Activate(Result);
				Camera = Result;
			}

			return Result;
		}
		Component* SceneGraph::GetComponent(uint64_t Component, uint64_t Section)
		{
			auto* Array = GetComponents(Section);
			if (Component >= Array->Size())
				return nullptr;

			return *Array->At(Component);
		}
		RenderSystem* SceneGraph::GetRenderer()
		{
			auto* Viewer = (Components::Camera*)Camera.load();
			if (!Viewer)
				return nullptr;

			return Viewer->GetRenderer();
		}
		Viewer SceneGraph::GetCameraViewer()
		{
			auto* Result = (Components::Camera*)Camera.load();
			if (!Result)
				return Viewer();

			return Result->GetViewer();
		}
		Material* SceneGraph::GetMaterial(const std::string& Name)
		{
			auto Begin = Materials.Begin(), End = Materials.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Name == Name)
					return *It;
			}

			return nullptr;
		}
		Material* SceneGraph::GetMaterial(uint64_t Material)
		{
			TH_ASSERT(Material < Materials.Size(), nullptr, "index outside of range");
			return Materials[Material];
		}
		Entity* SceneGraph::GetEntity(uint64_t Entity)
		{
			TH_ASSERT(Entity < Entities.Size(), nullptr, "index outside of range");
			return Entities[Entity];
		}
		Entity* SceneGraph::GetLastEntity()
		{
			if (Entities.Empty())
				return nullptr;

			return Entities.Back();
		}
		Entity* SceneGraph::FindNamedEntity(const std::string& Name)
		{
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Name == Name)
					return *It;
			}

			return nullptr;
		}
		Entity* SceneGraph::FindEntityAt(const Compute::Vector3& Position, float Radius)
		{
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Transform->GetPosition().Distance(Position) <= Radius + (*It)->Transform->GetScale().Length())
					return *It;
			}

			return nullptr;
		}
		Entity* SceneGraph::FindTaggedEntity(uint64_t Tag)
		{
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Tag == Tag)
					return *It;
			}

			return nullptr;
		}
		Entity* SceneGraph::CloneEntity(Entity* Entity)
		{
			TH_ASSERT(Entity != nullptr, nullptr, "entity should be set");
			TH_PPUSH("clone-entity", TH_PERF_FRAME);

			Engine::Entity* Instance = new Engine::Entity(this);
			Instance->Transform->Copy(Entity->Transform);
			Instance->Transform->UserPointer = Instance;
			Instance->Tag = Entity->Tag;
			Instance->Name = Entity->Name;
			Instance->Components = Entity->Components;

			for (auto& It : Instance->Components)
			{
				Component* Source = It.second;
				It.second = Source->Copy(Instance);
				It.second->Parent = Instance;
				It.second->Active = Source->Active;
			}

			AddEntity(Instance);
			TH_PPOP();

			return Instance;
		}
		Core::Pool<Component*>* SceneGraph::GetComponents(uint64_t Section)
		{
			Core::Pool<Component*>* Array = &Components[Section];
			if (Array->Capacity() >= Conf.ComponentCount)
				return Array;

			ExclusiveLock();
			Array->Reserve(Conf.ComponentCount);
			ExclusiveUnlock();

			return Array;
		}
		Core::Pool<Drawable*>* SceneGraph::GetOpaque(uint64_t Section)
		{
			Core::Pool<Drawable*>* Array = &Drawables[Section].Opaque;
			if (Array->Capacity() >= Conf.ComponentCount)
				return Array;

			ExclusiveLock();
			Array->Reserve(Conf.ComponentCount);
			ExclusiveUnlock();

			return Array;
		}
		Core::Pool<Drawable*>* SceneGraph::GetTransparent(uint64_t Section)
		{
			Core::Pool<Drawable*>* Array = &Drawables[Section].Transparent;
			if (Array->Capacity() >= Conf.ComponentCount)
				return Array;

			ExclusiveLock();
			Array->Reserve(Conf.ComponentCount);
			ExclusiveUnlock();

			return Array;
		}
		Core::Pool<Drawable*>* SceneGraph::GetAdditive(uint64_t Section)
		{
			Core::Pool<Drawable*>* Array = &Drawables[Section].Additive;
			if (Array->Capacity() >= Conf.ComponentCount)
				return Array;

			ExclusiveLock();
			Array->Reserve(Conf.ComponentCount);
			ExclusiveUnlock();

			return Array;
		}
		Graphics::RenderTarget2D::Desc SceneGraph::GetDescRT()
		{
			TH_ASSERT(Conf.Device != nullptr, Graphics::RenderTarget2D::Desc(), "graphics device should be set");
			Graphics::RenderTarget2D* Target = Conf.Device->GetRenderTarget();

			TH_ASSERT(Target != nullptr, Graphics::RenderTarget2D::Desc(), "render target should be set");
			Graphics::RenderTarget2D::Desc Desc;
			Desc.MiscFlags = Graphics::ResourceMisc::Generate_Mips;
			Desc.Width = (unsigned int)(Target->GetWidth() * Conf.RenderQuality);
			Desc.Height = (unsigned int)(Target->GetHeight() * Conf.RenderQuality);
			Desc.MipLevels = Conf.Device->GetMipLevel(Desc.Width, Desc.Height);
			Desc.FormatMode = GetFormatMRT(0);

			return Desc;
		}
		Graphics::MultiRenderTarget2D::Desc SceneGraph::GetDescMRT()
		{
			TH_ASSERT(Conf.Device != nullptr, Graphics::MultiRenderTarget2D::Desc(), "graphics device should be set");
			Graphics::RenderTarget2D* Target = Conf.Device->GetRenderTarget();

			TH_ASSERT(Target != nullptr, Graphics::MultiRenderTarget2D::Desc(), "render target should be set");
			Graphics::MultiRenderTarget2D::Desc Desc;
			Desc.MiscFlags = Graphics::ResourceMisc::Generate_Mips;
			Desc.Width = (unsigned int)(Target->GetWidth() * Conf.RenderQuality);
			Desc.Height = (unsigned int)(Target->GetHeight() * Conf.RenderQuality);
			Desc.MipLevels = Conf.Device->GetMipLevel(Desc.Width, Desc.Height);
			Desc.Target = Graphics::SurfaceTarget::T3;
			Desc.FormatMode[0] = GetFormatMRT(0);
			Desc.FormatMode[1] = GetFormatMRT(1);
			Desc.FormatMode[2] = GetFormatMRT(2);
			Desc.FormatMode[3] = GetFormatMRT(3);

			return Desc;
		}
		Graphics::Format SceneGraph::GetFormatMRT(unsigned int Target)
		{
			if (Target == 0)
				return Conf.EnableHDR ? Graphics::Format::R16G16B16A16_Unorm : Graphics::Format::R8G8B8A8_Unorm;

			if (Target == 1)
				return Graphics::Format::R16G16B16A16_Float;

			if (Target == 2)
				return Graphics::Format::R32_Float;

			if (Target == 3)
				return Graphics::Format::R8G8B8A8_Unorm;

			return Graphics::Format::Unknown;
		}
		std::vector<Entity*> SceneGraph::FindParentFreeEntities(Entity* Entity)
		{
			std::vector<Engine::Entity*> Array;
			TH_ASSERT(Entity != nullptr, Array, "entity should be set");

			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if (*It != Entity && !(*It)->Transform->HasRoot(Entity->Transform))
					Array.push_back(*It);
			}

			return Array;
		}
		std::vector<Entity*> SceneGraph::FindNamedEntities(const std::string& Name)
		{
			std::vector<Entity*> Array;
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Name == Name)
					Array.push_back(*It);
			}

			return Array;
		}
		std::vector<Entity*> SceneGraph::FindEntitiesAt(const Compute::Vector3& Position, float Radius)
		{
			std::vector<Entity*> Array;
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Transform->GetPosition().Distance(Position) <= Radius + (*It)->Transform->GetScale().Length())
					Array.push_back(*It);
			}

			return Array;
		}
		std::vector<Entity*> SceneGraph::FindTaggedEntities(uint64_t Tag)
		{
			std::vector<Entity*> Array;
			auto Begin = Entities.Begin(), End = Entities.End();
			for (auto It = Begin; It != End; ++It)
			{
				if ((*It)->Tag == Tag)
					Array.push_back(*It);
			}

			return Array;
		}
		bool SceneGraph::IsEntityVisible(Entity* Entity, RenderSystem* Renderer)
		{
			TH_ASSERT(Entity != nullptr, false, "entity should be set");
			TH_ASSERT(Renderer != nullptr, false, "renderer should be set");

			if (Entity->Transform->GetPosition().Distance(Renderer->View.Position) > Renderer->View.FarPlane + Entity->Transform->GetScale().Length())
				return false;

			return Compute::Common::IsCubeInFrustum(Entity->Transform->GetBias() * Renderer->View.ViewProjection, 2);
		}
		bool SceneGraph::IsEntityVisible(Entity* Entity, const Compute::Matrix4x4& ViewProjection, const Compute::Vector3& ViewPos, float DrawDistance)
		{
			TH_ASSERT(Entity != nullptr, false, "entity should be set");
			if (Entity->Transform->GetPosition().Distance(ViewPos) > DrawDistance + Entity->Transform->GetScale().Length())
				return false;

			return Compute::Common::IsCubeInFrustum(Entity->Transform->GetBias() * ViewProjection, 2);
		}
		bool SceneGraph::AddEntity(Entity* Entity)
		{
			TH_ASSERT(Entity != nullptr, false, "entity should be set");
			TH_ASSERT(Entity->Scene == this, false, "entity should be created for this scene");

			if (Entities.Add(Entity) == Entities.End())
				return false;

			RegisterEntity(Entity);
			return true;
		}
		bool SceneGraph::HasEntity(Entity* Entity)
		{
			TH_ASSERT(Entity != nullptr, false, "entity should be set");
			for (uint64_t i = 0; i < Entities.Size(); i++)
			{
				if (Entities[i] == Entity)
					return true;
			}

			return false;
		}
		bool SceneGraph::HasEntity(uint64_t Entity)
		{
			return Entity < Entities.Size() ? Entity : -1;
		}
		bool SceneGraph::IsActive()
		{
			return Active;
		}
		uint64_t SceneGraph::GetEntitiesCount()
		{
			return Entities.Size();
		}
		uint64_t SceneGraph::GetComponentsCount(uint64_t Section)
		{
			return Components[Section].Size();
		}
		uint64_t SceneGraph::GetMaterialsCount()
		{
			return Materials.Size();
		}
		uint64_t SceneGraph::GetOpaquesCount()
		{
			uint64_t Count = 0;
			for (auto& Array : Drawables)
				Count += Array.second.Opaque.Size();

			return Count;
		}
		uint64_t SceneGraph::GetTransparentsCount()
		{
			uint64_t Count = 0;
			for (auto& Array : Drawables)
				Count += Array.second.Transparent.Size();

			return Count;
		}
		uint64_t SceneGraph::GetAdditivesCount()
		{
			uint64_t Count = 0;
			for (auto& Array : Drawables)
				Count += Array.second.Additive.Size();

			return Count;
		}
		size_t SceneGraph::GetVoxelBufferSize()
		{
			return Display.VoxelSize;
		}
		Graphics::MultiRenderTarget2D* SceneGraph::GetMRT(TargetType Type)
		{
			return Display.MRT[(size_t)Type];
		}
		Graphics::RenderTarget2D* SceneGraph::GetRT(TargetType Type)
		{
			return Display.RT[(size_t)Type];
		}
		Graphics::Texture2D** SceneGraph::GetMerger()
		{
			return &Display.Merger;
		}
		Graphics::ElementBuffer* SceneGraph::GetStructure()
		{
			return Display.MaterialBuffer;
		}
		Graphics::GraphicsDevice* SceneGraph::GetDevice()
		{
			return Conf.Device;
		}
		ShaderCache* SceneGraph::GetShaders()
		{
			return Conf.Shaders;
		}
		PrimitiveCache* SceneGraph::GetPrimitives()
		{
			return Conf.Primitives;
		}
		Compute::Simulator* SceneGraph::GetSimulator()
		{
			return Simulator;
		}
		SceneGraph::Desc& SceneGraph::GetConf()
		{
			return Conf;
		}

		ContentManager::ContentManager(Graphics::GraphicsDevice* NewDevice) : Device(NewDevice), Base(Core::OS::Path::ResolveDirectory(Core::OS::Directory::Get().c_str()))
		{
			SetEnvironment(Base);
		}
		ContentManager::~ContentManager()
		{
			InvalidateCache();
			InvalidateDockers();

			for (auto It = Streams.begin(); It != Streams.end(); ++It)
				TH_RELEASE(It->first);

			for (auto It = Processors.begin(); It != Processors.end(); ++It)
				TH_RELEASE(It->second);
		}
		void ContentManager::InvalidateDockers()
		{
			Mutex.lock();
			for (auto It = Dockers.begin(); It != Dockers.end(); ++It)
				TH_DELETE(AssetArchive, It->second);

			Dockers.clear();
			Mutex.unlock();
		}
		void ContentManager::InvalidateCache()
		{
			Mutex.lock();
			for (auto& Entries : Assets)
			{
				for (auto& Entry : Entries.second)
				{
					if (!Entry.second)
						continue;

					Mutex.unlock();
					if (Entry.first != nullptr)
						Entry.first->Free(Entry.second);
					Mutex.lock();

					TH_DELETE(AssetCache, Entry.second);
				}
			}

			Assets.clear();
			Mutex.unlock();
		}
		void ContentManager::InvalidatePath(const std::string& Path)
		{
			std::string File = Core::OS::Path::ResolveResource(Path, Environment);
			if (File.empty())
				return;

			Mutex.lock();
			auto It = Assets.find(Core::Parser(File).Replace('\\', '/').Replace(Environment, "./").R());
			if (It != Assets.end())
				Assets.erase(It);
			Mutex.unlock();
		}
		void ContentManager::SetEnvironment(const std::string& Path)
		{
			Mutex.lock();
			Environment = Core::OS::Path::ResolveDirectory(Path.c_str());
			Core::Parser(&Environment).Replace('\\', '/');
			Core::OS::Directory::Set(Environment.c_str());
			Mutex.unlock();
		}
		void ContentManager::SetDevice(Graphics::GraphicsDevice* NewDevice)
		{
			Device = NewDevice;
		}
		void* ContentManager::LoadForward(const std::string& Path, Processor* Processor, const Core::VariantArgs& Map)
		{
			if (Path.empty())
				return nullptr;

			if (!Processor)
			{
				TH_ERR("file processor for \"%s\" wasn't found", Path.c_str());
				return nullptr;
			}

			void* Object = LoadStreaming(Path, Processor, Map);
			if (Object != nullptr)
				return Object;

			std::string File = Path;
			if (!Core::OS::Path::IsRemote(Path.c_str()))
			{
				Mutex.lock();
				File = Core::OS::Path::ResolveResource(Path, Environment);
				Mutex.unlock();

				if (File.empty())
				{
					TH_ERR("file \"%s\" wasn't found", Path.c_str());
					return nullptr;
				}
			}

			AssetCache* Asset = Find(Processor, File);
			if (Asset != nullptr)
				return Processor->Duplicate(Asset, Map);

			auto* Stream = Core::OS::File::Open(File, Core::FileMode::Binary_Read_Only);
			if (!Stream)
				return nullptr;

			Object = Processor->Deserialize(Stream, Stream->GetSize(), 0, Map);
			TH_RELEASE(Stream);

			return Object;
		}
		void* ContentManager::LoadStreaming(const std::string& Path, Processor* Processor, const Core::VariantArgs& Map)
		{
			if (Path.empty())
				return nullptr;

			Core::Parser File(Path);
			File.Replace('\\', '/').Replace("./", "");

			auto Docker = Dockers.find(File.R());
			if (Docker == Dockers.end() || !Docker->second || !Docker->second->Stream)
				return nullptr;

			AssetCache* Asset = Find(Processor, File.R());
			if (Asset != nullptr)
				return Processor->Duplicate(Asset, Map);

			auto It = Streams.find(Docker->second->Stream);
			if (It == Streams.end())
			{
				TH_ERR("cannot resolve stream offset for \"%s\"", Path.c_str());
				return nullptr;
			}

			auto* Stream = Docker->second->Stream;
			Stream->Seek(Core::FileSeek::Begin, It->second + Docker->second->Offset);
			Stream->GetSource() = File.R();

			return Processor->Deserialize(Stream, Docker->second->Length, It->second + Docker->second->Offset, Map);
		}
		bool ContentManager::SaveForward(const std::string& Path, Processor* Processor, void* Object, const Core::VariantArgs& Map)
		{
			TH_ASSERT(Object != nullptr, false, "object should be set");
			if (Path.empty())
				return false;

			if (!Processor)
			{
				TH_ERR("file processor for \"%s\" wasn't found", Path.c_str());
				return false;
			}

			Mutex.lock();
			std::string Directory = Core::OS::Path::GetDirectory(Path.c_str());
			std::string File = Core::OS::Path::Resolve(Directory, Environment);
			File.append(Path.substr(Directory.size()));
			Mutex.unlock();

			auto* Stream = Core::OS::File::Open(File, Core::FileMode::Binary_Write_Only);
			if (!Stream)
			{
				Stream = Core::OS::File::Open(Path, Core::FileMode::Binary_Write_Only);
				if (!Stream)
				{
					TH_ERR("cannot open stream for writing at \"%s\" or \"%s\"", File.c_str(), Path.c_str());
					TH_RELEASE(Stream);
					return false;
				}
			}

			bool Result = Processor->Serialize(Stream, Object, Map);
			TH_RELEASE(Stream);

			return Result;
		}
		bool ContentManager::Import(const std::string& Path)
		{
			Mutex.lock();
			std::string File = Core::OS::Path::ResolveResource(Path, Environment);
			Mutex.unlock();

			if (File.empty())
			{
				TH_ERR("file \"%s\" wasn't found", Path.c_str());
				return false;
			}

			auto* Stream = new Core::GzStream();
			if (!Stream->Open(File.c_str(), Core::FileMode::Binary_Read_Only))
			{
				TH_ERR("cannot open \"%s\" for reading", File.c_str());
				TH_RELEASE(Stream);

				return false;
			}

			char Buffer[16];
			if (Stream->Read(Buffer, 16) != 16)
			{
				TH_ERR("file \"%s\" has corrupted header", File.c_str());
				TH_RELEASE(Stream);

				return false;
			}

			if (memcmp(Buffer, "\0d\0o\0c\0k\0h\0e\0a\0d", sizeof(char) * 16) != 0)
			{
				TH_ERR("file \"%s\" header version is corrupted", File.c_str());
				TH_RELEASE(Stream);

				return false;
			}

			uint64_t Size = 0;
			if (Stream->Read((char*)&Size, sizeof(uint64_t)) != sizeof(uint64_t))
			{
				TH_ERR("file \"%s\" has corrupted dock size", File.c_str());
				TH_RELEASE(Stream);

				return false;
			}

			Mutex.lock();
			for (uint64_t i = 0; i < Size; i++)
			{
				AssetArchive* Docker = TH_NEW(AssetArchive);
				Docker->Stream = Stream;

				uint64_t Length;
				Stream->Read((char*)&Length, sizeof(uint64_t));
				Stream->Read((char*)&Docker->Offset, sizeof(uint64_t));
				Stream->Read((char*)&Docker->Length, sizeof(uint64_t));

				if (!Length)
				{
					TH_DELETE(AssetArchive, Docker);
					continue;
				}

				Docker->Path.resize(Length);
				Stream->Read((char*)Docker->Path.c_str(), sizeof(char) * Length);
				Dockers[Docker->Path] = Docker;
			}

			Streams[Stream] = (int64_t)Stream->Tell();
			Mutex.unlock();

			return true;
		}
		bool ContentManager::Export(const std::string& Path, const std::string& Directory, const std::string& Name)
		{
			TH_ASSERT(!Path.empty() && !Directory.empty(), false, "path and directory should not be empty");
			auto* Stream = new Core::GzStream();
			if (!Stream->Open(Core::OS::Path::Resolve(Path, Environment).c_str(), Core::FileMode::Write_Only))
			{
				TH_ERR("cannot open \"%s\" for writing", Path.c_str());
				TH_RELEASE(Stream);
				return false;
			}

			std::string DBase = Core::OS::Path::Resolve(Directory, Environment);
			auto Tree = new Core::FileTree(DBase);
			Stream->Write("\0d\0o\0c\0k\0h\0e\0a\0d", sizeof(char) * 16);

			uint64_t Size = Tree->GetFiles();
			Stream->Write((char*)&Size, sizeof(uint64_t));

			uint64_t Offset = 0;
			Tree->Loop([Stream, &Offset, &DBase, &Name](Core::FileTree* Tree)
			{
				for (auto& Resource : Tree->Files)
				{
					auto* File = Core::OS::File::Open(Resource, Core::FileMode::Binary_Read_Only);
					if (!File)
						continue;

					std::string Path = Core::Parser(Resource).Replace(DBase, Name).Replace('\\', '/').R();
					if (Name.empty())
						Path.assign(Path.substr(1));

					uint64_t Size = (uint64_t)Path.size();
					uint64_t Length = File->GetSize();

					Stream->Write((char*)&Size, sizeof(uint64_t));
					Stream->Write((char*)&Offset, sizeof(uint64_t));
					Stream->Write((char*)&Length, sizeof(uint64_t));

					Offset += Length;
					if (Size > 0)
						Stream->Write((char*)Path.c_str(), sizeof(char) * Size);

					TH_RELEASE(File);
				}

				return true;
			});
			Tree->Loop([Stream](Core::FileTree* Tree)
			{
				for (auto& Resource : Tree->Files)
				{
					auto* File = Core::OS::File::Open(Resource, Core::FileMode::Binary_Read_Only);
					if (!File)
						continue;

					int64_t Size = (int64_t)File->GetSize();
					while (Size > 0)
					{
						char Buffer[8192];
						int64_t Offset = File->Read(Buffer, Size > 8192 ? 8192 : Size);
						if (Offset <= 0)
							break;

						Stream->Write(Buffer, Offset);
						Size -= Offset;
					}

					TH_RELEASE(File);
				}

				return true;
			});

			TH_RELEASE(Tree);
			TH_RELEASE(Stream);

			return true;
		}
		bool ContentManager::Cache(Processor* Root, const std::string& Path, void* Resource)
		{
			if (Find(Root, Path) != nullptr)
				return false;

			AssetCache* Asset = TH_NEW(AssetCache);
			Asset->Path = Core::Parser(Path).Replace('\\', '/').Replace(Environment, "./").R();
			Asset->Resource = Resource;

			Mutex.lock();
			auto& Entries = Assets[Asset->Path];
			Entries[Root] = Asset;
			Mutex.unlock();

			return true;
		}
		AssetCache* ContentManager::Find(Processor* Target, const std::string& Path)
		{
			Mutex.lock();
			auto It = Assets.find(Core::Parser(Path).Replace('\\', '/').Replace(Environment, "./").R());
			if (It != Assets.end())
			{
				auto KIt = It->second.find(Target);
				if (KIt != It->second.end())
				{
					Mutex.unlock();
					return KIt->second;
				}
			}

			Mutex.unlock();
			return nullptr;
		}
		AssetCache* ContentManager::Find(Processor* Target, void* Resource)
		{
			Mutex.lock();
			for (auto& It : Assets)
			{
				auto KIt = It.second.find(Target);
				if (KIt == It.second.end())
					continue;

				if (KIt->second && KIt->second->Resource == Resource)
				{
					Mutex.unlock();
					return KIt->second;
				}
			}

			Mutex.unlock();
			return nullptr;
		}
		Graphics::GraphicsDevice* ContentManager::GetDevice()
		{
			return Device;
		}
		std::string ContentManager::GetEnvironment()
		{
			return Environment;
		}

		Application::Application(Desc* I) : Control(I ? *I : Desc())
		{
			TH_ASSERT_V(I != nullptr, "desc should be set");
			Host = this;

			if (I->Usage & (size_t)ApplicationSet::ContentSet)
			{
				Content = new ContentManager(nullptr);
				Content->AddProcessor<Processors::Asset, Engine::AssetFile>();
				Content->AddProcessor<Processors::Material, Engine::Material>();
				Content->AddProcessor<Processors::SceneGraph, Engine::SceneGraph>();
				Content->AddProcessor<Processors::AudioClip, Audio::AudioClip>();
				Content->AddProcessor<Processors::Texture2D, Graphics::Texture2D>();
				Content->AddProcessor<Processors::Shader, Graphics::Shader>();
				Content->AddProcessor<Processors::Model, Graphics::Model>();
				Content->AddProcessor<Processors::SkinModel, Graphics::SkinModel>();
				Content->AddProcessor<Processors::Document, Core::Document>();
				Content->AddProcessor<Processors::Server, Network::HTTP::Server>();
				Content->AddProcessor<Processors::HullShape, Compute::HullShape>();
				Content->SetEnvironment(I->Environment.empty() ? Core::OS::Directory::Get() + I->Directory : I->Environment + I->Directory);
			}
#ifdef TH_HAS_SDL2
			if (I->Usage & (size_t)ApplicationSet::ActivitySet)
			{
				if (!I->Activity.Width || !I->Activity.Height)
				{
					SDL_DisplayMode Display;
					SDL_GetCurrentDisplayMode(0, &Display);
					I->Activity.Width = Display.w / 1.1;
					I->Activity.Height = Display.h / 1.2;
				}

				if (I->Activity.Width > 0 && I->Activity.Height > 0)
				{
					I->Activity.AllowGraphics = (I->Usage & (size_t)ApplicationSet::GraphicsSet);
					Activity = new Graphics::Activity(I->Activity);
					if (I->Activity.AllowGraphics)
					{
						if (!I->GraphicsDevice.BufferWidth)
							I->GraphicsDevice.BufferWidth = I->Activity.Width;

						if (!I->GraphicsDevice.BufferHeight)
							I->GraphicsDevice.BufferHeight = I->Activity.Height;

						I->GraphicsDevice.Window = Activity;
						if (Content != nullptr && !I->GraphicsDevice.CacheDirectory.empty())
							I->GraphicsDevice.CacheDirectory = Core::OS::Path::ResolveDirectory(I->GraphicsDevice.CacheDirectory, Content->GetEnvironment());

						Renderer = Graphics::GraphicsDevice::Create(I->GraphicsDevice);
						if (!Renderer || !Renderer->IsValid())
						{
							TH_ERR("graphics device cannot be created");
							return;
						}

						Compute::Common::SetLeftHanded(Renderer->IsLeftHanded());
						if (Content != nullptr)
							Content->SetDevice(Renderer);

						Cache.Shaders = new ShaderCache(Renderer);
						Cache.Primitives = new PrimitiveCache(Renderer);
					}
					else if (!Activity->GetHandle())
					{
						TH_ERR("cannot create activity instance");
						return;
					}

					Activity->UserPointer = this;
					Activity->SetCursorVisibility(I->Cursor);
					Activity->Callbacks.KeyState = [this](Graphics::KeyCode Key, Graphics::KeyMod Mod, int Virtual, int Repeat, bool Pressed)
					{
#ifdef TH_WITH_RMLUI
						GUI::Context* GUI = (GUI::Context*)GetGUI();
						if (GUI != nullptr)
							GUI->EmitKey(Key, Mod, Virtual, Repeat, Pressed);
#endif
						KeyEvent(Key, Mod, Virtual, Repeat, Pressed);
					};
					Activity->Callbacks.Input = [this](char* Buffer, int Length)
					{
#ifdef TH_WITH_RMLUI
						GUI::Context* GUI = (GUI::Context*)GetGUI();
						if (GUI != nullptr)
							GUI->EmitInput(Buffer, Length);
#endif
						InputEvent(Buffer, Length);
					};
					Activity->Callbacks.CursorWheelState = [this](int X, int Y, bool Normal)
					{
#ifdef TH_WITH_RMLUI
						GUI::Context* GUI = (GUI::Context*)GetGUI();
						if (GUI != nullptr)
							GUI->EmitWheel(X, Y, Normal, Activity->GetKeyModState());
#endif
						WheelEvent(X, Y, Normal);
					};
					Activity->Callbacks.WindowStateChange = [this](Graphics::WindowState NewState, int X, int Y)
					{
#ifdef TH_WITH_RMLUI
						if (NewState == Graphics::WindowState::Resize)
						{
							GUI::Context* GUI = (GUI::Context*)GetGUI();
							if (GUI != nullptr)
								GUI->EmitResize(X, Y);
						}
#endif
						WindowEvent(NewState, X, Y);
					};

				}
				else
					TH_ERR("cannot detect display to create activity");
			}
#endif
			if (I->Usage & (size_t)ApplicationSet::AudioSet)
			{
				Audio = new Audio::AudioDevice();
				if (!Audio->IsValid())
				{
					TH_ERR("audio device cannot be created");
					return;
				}
			}

			if (I->Usage & (size_t)ApplicationSet::ScriptSet)
				VM = new Script::VMManager();
#ifdef TH_WITH_RMLUI
			if (Activity != nullptr && Renderer != nullptr && Content != nullptr)
			{
				GUI::Subsystem::SetMetadata(Activity, Content, nullptr);
				GUI::Subsystem::SetManager(VM);
			}
#endif
			NetworkQueue = (I->Usage & (size_t)ApplicationSet::NetworkSet);
			if (NetworkQueue)
				Network::Driver::Create(256, I->Async ? 100 : 0);

			State = ApplicationState::Staging;
		}
		Application::~Application()
		{
			if (Renderer != nullptr)
				Renderer->FlushState();

			TH_RELEASE(Scene);
			TH_RELEASE(VM);
			TH_RELEASE(Audio);
#ifdef TH_WITH_RMLUI
			if (Activity != nullptr && Renderer != nullptr && Content != nullptr)
				Engine::GUI::Subsystem::Release();
#endif
			TH_RELEASE(Cache.Shaders);
			TH_RELEASE(Cache.Primitives);
			TH_RELEASE(Content);
			TH_RELEASE(Renderer);
			TH_RELEASE(Activity);

			if (NetworkQueue)
				Network::Driver::Release();

			Host = nullptr;
		}
		void Application::ScriptHook(Script::VMGlobal* Global)
		{
		}
		void Application::KeyEvent(Graphics::KeyCode Key, Graphics::KeyMod Mod, int Virtual, int Repeat, bool Pressed)
		{
		}
		void Application::InputEvent(char* Buffer, int Length)
		{
		}
		void Application::WheelEvent(int X, int Y, bool Normal)
		{
		}
		void Application::WindowEvent(Graphics::WindowState NewState, int X, int Y)
		{
		}
		void Application::CloseEvent()
		{
		}
		bool Application::ComposeEvent()
		{
			return false;
		}
		void Application::Dispatch(Core::Timer* Time)
		{
		}
		void Application::Publish(Core::Timer* Time)
		{
		}
		void Application::Initialize()
		{
		}
		void Application::Start()
		{
			if (!ComposeEvent())
				Compose();

			if (Control.Usage & (size_t)ApplicationSet::ActivitySet && !Activity)
			{
				TH_ERR("[conf] activity was not found");
				return;
			}

			if (Control.Usage & (size_t)ApplicationSet::GraphicsSet && !Renderer)
			{
				TH_ERR("[conf] graphics device was not found");
				return;
			}

			if (Control.Usage & (size_t)ApplicationSet::AudioSet && !Audio)
			{
				TH_ERR("[conf] audio device was not found");
				return;
			}

			if (Control.Usage & (size_t)ApplicationSet::ScriptSet)
			{
				if (!VM)
				{
					TH_ERR("[conf] vm was not found");
					return;
				}
				else
					ScriptHook(&VM->Global());
			}

			Initialize();
			if (State == ApplicationState::Terminated)
				return;

			Core::Timer* Time = new Core::Timer();
			Time->FrameLimit = Control.Framerate;
			Time->SetStepLimitation(Control.MinFrames, Control.MaxFrames);

			Core::Schedule* Queue = Core::Schedule::Get();
			if (NetworkQueue)
				Queue->SetTask(Network::Driver::Multiplex);
#ifdef TH_WITH_RMLUI
			if (Activity != nullptr && Renderer != nullptr && Content != nullptr)
				GUI::Subsystem::SetMetadata(Activity, Content, Time);
#endif
			ApplicationState OK;
			if (Control.Async)
				OK = State = ApplicationState::Multithreaded;
			else
				OK = State = ApplicationState::Singlethreaded;

			Queue->Start(Control.Async, Control.Threads, Control.Coroutines, Control.Stack);
			if (Activity != nullptr && Control.Async)
			{
				while (State == OK)
				{
					Activity->Dispatch();
					Time->Synchronize();
					Dispatch(Time);
					Publish(Time);
					TH_OSIG();
				}
			}
			else if (Activity != nullptr && !Control.Async)
			{
				while (State == OK)
				{
					Queue->Dispatch();
					Activity->Dispatch();
					Time->Synchronize();
					Dispatch(Time);
					Publish(Time);
					TH_OSIG();
				}
			}
			else if (!Activity && Control.Async)
			{
				while (State == OK)
				{
					Time->Synchronize();
					Dispatch(Time);
					Publish(Time);
					TH_OSIG();
				}
			}
			else if (!Activity && !Control.Async)
			{
				while (State == OK)
				{
					Queue->Dispatch();
					Time->Synchronize();
					Dispatch(Time);
					Publish(Time);
					TH_OSIG();
				}
			}

			CloseEvent();
			Queue->Stop();
			TH_RELEASE(Time);
		}
		void Application::Stop()
		{
			State = ApplicationState::Terminated;
		}
		void* Application::GetGUI()
		{
#ifdef TH_WITH_RMLUI
			if (!Scene)
				return nullptr;

			auto* Viewer = (Components::Camera*)Scene->GetCamera();
			if (!Viewer)
				return nullptr;

			Renderers::UserInterface* Result = Viewer->GetRenderer()->GetRenderer<Renderers::UserInterface>();
			return Result != nullptr ? Result->GetContext() : nullptr;
#else
			return nullptr;
#endif
		}
		ApplicationState Application::GetState()
		{
			return State;
		}
		void Application::Compose()
		{
			Core::Composer::Push<Components::RigidBody, Entity*>();
			Core::Composer::Push<Components::SoftBody, Entity*>();
			Core::Composer::Push<Components::Acceleration, Entity*>();
			Core::Composer::Push<Components::SliderConstraint, Entity*>();
			Core::Composer::Push<Renderers::SoftBody, RenderSystem*>();
			Core::Composer::Push<Components::Model, Entity*>();
			Core::Composer::Push<Components::Skin, Entity*>();
			Core::Composer::Push<Components::Emitter, Entity*>();
			Core::Composer::Push<Components::Decal, Entity*>();
			Core::Composer::Push<Components::SkinAnimator, Entity*>();
			Core::Composer::Push<Components::KeyAnimator, Entity*>();
			Core::Composer::Push<Components::EmitterAnimator, Entity*>();
			Core::Composer::Push<Components::FreeLook, Entity*>();
			Core::Composer::Push<Components::Fly, Entity*>();
			Core::Composer::Push<Components::AudioSource, Entity*>();
			Core::Composer::Push<Components::AudioListener, Entity*>();
			Core::Composer::Push<Components::PointLight, Entity*>();
			Core::Composer::Push<Components::SpotLight, Entity*>();
			Core::Composer::Push<Components::LineLight, Entity*>();
			Core::Composer::Push<Components::SurfaceLight, Entity*>();
			Core::Composer::Push<Components::Illuminator, Entity*>();
			Core::Composer::Push<Components::Camera, Entity*>();
			Core::Composer::Push<Components::Scriptable, Entity*>();
			Core::Composer::Push<Renderers::Model, RenderSystem*>();
			Core::Composer::Push<Renderers::Skin, RenderSystem*>();
			Core::Composer::Push<Renderers::Emitter, RenderSystem*>();
			Core::Composer::Push<Renderers::Decal, RenderSystem*>();
			Core::Composer::Push<Renderers::Lighting, RenderSystem*>();
			Core::Composer::Push<Renderers::Transparency, RenderSystem*>();
			Core::Composer::Push<Renderers::Glitch, RenderSystem*>();
			Core::Composer::Push<Renderers::Tone, RenderSystem*>();
			Core::Composer::Push<Renderers::DoF, RenderSystem*>();
			Core::Composer::Push<Renderers::Bloom, RenderSystem*>();
			Core::Composer::Push<Renderers::SSR, RenderSystem*>();
			Core::Composer::Push<Renderers::SSAO, RenderSystem*>();
			Core::Composer::Push<Renderers::MotionBlur, RenderSystem*>();
			Core::Composer::Push<Renderers::UserInterface, RenderSystem*>();
			Core::Composer::Push<Audio::Effects::Reverb>();
			Core::Composer::Push<Audio::Effects::Chorus>();
			Core::Composer::Push<Audio::Effects::Distortion>();
			Core::Composer::Push<Audio::Effects::Echo>();
			Core::Composer::Push<Audio::Effects::Flanger>();
			Core::Composer::Push<Audio::Effects::FrequencyShifter>();
			Core::Composer::Push<Audio::Effects::VocalMorpher>();
			Core::Composer::Push<Audio::Effects::PitchShifter>();
			Core::Composer::Push<Audio::Effects::RingModulator>();
			Core::Composer::Push<Audio::Effects::Autowah>();
			Core::Composer::Push<Audio::Effects::Compressor>();
			Core::Composer::Push<Audio::Effects::Equalizer>();
			Core::Composer::Push<Audio::Filters::Lowpass>();
			Core::Composer::Push<Audio::Filters::Bandpass>();
			Core::Composer::Push<Audio::Filters::Highpass>();
		}
		Core::Schedule* Application::Queue()
		{
			return Core::Schedule::Get();
		}
		Application* Application::Get()
		{
			return Host;
		}
		Application* Application::Host = nullptr;
	}
}
