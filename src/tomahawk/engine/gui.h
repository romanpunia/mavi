#ifndef TH_ENGINE_GUI_H
#define TH_ENGINE_GUI_H

#include "../core/engine.h"

namespace Rml
{
	class Context;

	class Element;

	class ElementDocument;

	class Decorator;

	class DataModelConstructor;

	class DataModelHandle;

	class Event;

	class EventListener;

	class Variant;
}

namespace Tomahawk
{
	namespace Engine
	{
		namespace GUI
		{
			class RenderSubsystem;

			class FileSubsystem;

			class MainSubsystem;

			class ScopedContext;

			class ContextInstancer;

			class ListenerSubsystem;

			class ListenerInstancer;

			class DocumentSubsystem;

			class DocumentInstancer;

			class EventSubsystem;

			class DataSourceSubsystem;

			class DataFormatterSubsystem;

			class IEvent;

			class DataModel;

			class DataSource;

			class DataRow;

			class IElement;

			class IElementDocument;

			class DataNode;
			
			class Handler;

			class Context;

			typedef std::function<std::string(const std::string&)> TranslationCallback;
			typedef std::function<void(DataRow*, const std::string&, std::string&)> ColumnCallback;
			typedef std::function<void(const std::vector<std::string>&, std::string&)> FormatCallback;
			typedef std::function<void(void*)> DestroyCallback;
			typedef std::function<void(DataRow*)> ChangeCallback;
			typedef std::function<void(IEvent&)> EventCallback;
			typedef std::function<void(IEvent&, const Compute::PropertyList&)> DataCallback;
			typedef std::function<void(Compute::Property&)> GetterCallback;
			typedef std::function<void(const Compute::Property&)> SetterCallback;
			typedef std::function<void(Context*)> ModelCallback;

			enum ModalFlag
			{
				ModalFlag_None,
				ModalFlag_Modal,
				ModalFlag_Keep
			};

			enum FocusFlag
			{
				FocusFlag_None,
				FocusFlag_Document,
				FocusFlag_Keep,
				FocusFlag_Auto
			};

			enum Area
			{
				Area_Margin = 0,
				Area_Border = 1,
				Area_Padding = 2,
				Area_Content = 3
			};

			enum Display : uint8_t
			{
				Display_None,
				Display_Block,
				Display_Inline,
				Display_InlineBlock,
				Display_Table,
				Display_TableRow,
				Display_TableRowGroup,
				Display_TableColumn,
				Display_TableColumnGroup,
				Display_TableCell
			};

			enum Position : uint8_t
			{
				Position_Static,
				Position_Relative,
				Position_Absolute,
				Position_Fixed
			};

			enum Float : uint8_t
			{
				Float_None,
				Float_Left,
				Float_Right
			};

			enum TimingFunc
			{
				TimingFunc_None,
				TimingFunc_Back,
				TimingFunc_Bounce,
				TimingFunc_Circular,
				TimingFunc_Cubic,
				TimingFunc_Elastic,
				TimingFunc_Exponential,
				TimingFunc_Linear,
				TimingFunc_Quadratic,
				TimingFunc_Quartic,
				TimingFunc_Quintic,
				TimingFunc_Sine,
				TimingFunc_Callback
			};

			enum TimingDir
			{
				TimingDir_In = 1,
				TimingDir_Out = 2,
				TimingDir_InOut = 3
			};

			enum EventPhase
			{
				EventPhase_None,
				EventPhase_Capture = 1,
				EventPhase_Target = 2,
				EventPhase_Bubble = 4
			};

			enum InputType
			{
				InputType_Keys = 1,
				InputType_Scroll = 2,
				InputType_Text = 3,
				InputType_Cursor = 4,
				InputType_Any = (InputType_Keys | InputType_Scroll | InputType_Text | InputType_Cursor)
			};

			class TH_OUT IEvent
			{
				friend EventSubsystem;
				friend DataModel;

			private:
				Rml::Event* Base;

			private:
				IEvent(Rml::Event* Ref);

			public:
				EventPhase GetPhase() const;
				void SetPhase(EventPhase Phase);
				void SetCurrentElement(const IElement& Element);
				IElement GetCurrentElement() const;
				IElement GetTargetElement() const;
				std::string GetType() const;
				void StopPropagation();
				void StopImmediatePropagation();
				bool IsInterruptible() const;
				bool IsPropagating() const;
				bool IsImmediatePropagating() const;
				bool GetBoolean(const std::string& Key) const;
				int64_t GetInteger(const std::string& Key) const;
				double GetNumber(const std::string& Key) const;
				std::string GetString(const std::string& Key) const;
				Compute::Vector2 GetVector2(const std::string& Key) const;
				Compute::Vector3 GetVector3(const std::string& Key) const;
				Compute::Vector4 GetVector4(const std::string& Key) const;
				void* GetPointer(const std::string& Key) const;
				Rml::Event* GetEvent() const;
				bool IsValid() const;
			};

			class TH_OUT IElement
			{
			protected:
				Rml::Element* Base;

			public:
				IElement(Rml::Element* Ref);
				virtual void Release();
				IElement Clone() const;
				void SetClass(const std::string& ClassName, bool Activate);
				bool IsClassSet(const std::string& ClassName) const;
				void SetClassNames(const std::string& ClassNames);
				std::string GetClassNames() const;
				std::string GetAddress(bool IncludePseudoClasses = false, bool IncludeParents = true) const;
				void SetOffset(const Compute::Vector2& Offset, const IElement& OffsetParent, bool OffsetFixed = false);
				Compute::Vector2 GetRelativeOffset(Area Type = Area_Content);
				Compute::Vector2 GetAbsoluteOffset(Area Type = Area_Content);
				void SetClientArea(Area ClientArea);
				Area GetClientArea() const;
				void SetContentBox(const Compute::Vector2& ContentOffset, const Compute::Vector2& ContentBox);
				float GetBaseline() const;
				bool GetIntrinsicDimensions(Compute::Vector2& Dimensions, float& Ratio);
				bool IsPointWithinElement(const Compute::Vector2& Point);
				bool IsVisible() const;
				float GetZIndex() const;
				bool SetProperty(const std::string& Name, const std::string& Value);
				void RemoveProperty(const std::string& Name);
				std::string GetProperty(const std::string& Name);
				std::string GetLocalProperty(const std::string& Name);
				float ResolveNumericProperty(const std::string& PropertyName);
				Compute::Vector2 GetContainingBlock();
				Position GetPosition();
				Float GetFloat();
				Display GetDisplay();
				float GetLineHeight();
				bool Project(Compute::Vector2& Point) const noexcept;
				bool Animate(const std::string& PropertyName, const std::string& TargetValue, float Duration, TimingFunc Func, TimingDir Dir, int NumIterations = 1, bool AlternateDirection = true, float Delay = 0.0f);
				bool AddAnimationKey(const std::string& PropertyName, const std::string& TargetValue, float Duration, TimingFunc Func, TimingDir Dir);
				void SetPseudoClass(const std::string& PseudoClass, bool Activate);
				bool IsPseudoClassSet(const std::string& PseudoClass) const;
				void SetAttribute(const std::string& Name, const std::string& Value);
				std::string GetAttribute(const std::string& Name);
				bool HasAttribute(const std::string& Name) const;
				void RemoveAttribute(const std::string& Name);
				IElement GetFocusLeafNode();
				std::string GetTagName() const;
				std::string GetId() const;
				void SetId(const std::string& Id);
				float GetAbsoluteLeft();
				float GetAbsoluteTop();
				float GetClientLeft();
				float GetClientTop();
				float GetClientWidth();
				float GetClientHeight();
				IElement GetOffsetParent();
				float GetOffsetLeft();
				float GetOffsetTop();
				float GetOffsetWidth();
				float GetOffsetHeight();
				float GetScrollLeft();
				void SetScrollLeft(float ScrollLeft);
				float GetScrollTop();
				void SetScrollTop(float ScrollTop);
				float GetScrollWidth();
				float GetScrollHeight();
				IElementDocument GetOwnerDocument() const;
				IElement GetParentNode() const;
				IElement GetNextSibling() const;
				IElement GetPreviousSibling() const;
				IElement GetFirstChild() const;
				IElement GetLastChild() const;
				IElement GetChild(int Index) const;
				int GetNumChildren(bool IncludeNonDOMElements = false) const;
				void GetInnerHTML(std::string& Content) const;
				std::string GetInnerHTML() const;
				void SetInnerHTML(const std::string& HTML);
				bool IsFocused();
				bool IsHovered();
				bool IsActive();
				bool IsChecked();
				bool Focus();
				void Blur();
				void Click();
				void AddEventListener(const std::string& Event, Handler* Listener, bool InCapturePhase = false);
				void RemoveEventListener(const std::string& Event, Handler* Listener, bool InCapturePhase = false);
				bool DispatchEvent(const std::string& Type, const Compute::PropertyArgs& Args);
				void ScrollIntoView(bool AlignWithTop = true);
				IElement AppendChild(const IElement& Element, bool DOMElement = true);
				IElement InsertBefore(const IElement& Element, const IElement& AdjacentElement);
				IElement ReplaceChild(const IElement& InsertedElement, const IElement& ReplacedElement);
				IElement RemoveChild(const IElement& Element);
				bool HasChildNodes() const;
				IElement GetElementById(const std::string& Id);
				IElement QuerySelector(const std::string& Selector);
				std::vector<IElement> QuerySelectorAll(const std::string& Selectors);
				int GetClippingIgnoreDepth();
				bool IsClippingEnabled();
				bool CastFormColor(Compute::Vector4* Ptr, bool Alpha);
				bool CastFormString(std::string* Ptr);
				bool CastFormInt32(int32_t* Ptr);
				bool CastFormUInt32(uint32_t* Ptr);
				bool CastFormFlag32(uint32_t* Ptr, uint32_t Mask);
				bool CastFormInt64(int64_t* Ptr);
				bool CastFormUInt64(uint64_t* Ptr);
				bool CastFormFlag64(uint64_t* Ptr, uint64_t Mask);
				bool CastFormFloat(float* Ptr);
				bool CastFormFloat(float* Ptr, float Mult);
				bool CastFormDouble(double* Ptr);
				bool CastFormBoolean(bool* Ptr);
				std::string GetFormName() const;
				void SetFormName(const std::string& Name);
				std::string GetFormValue() const;
				void SetFormValue(const std::string& Value);
				bool IsFormDisabled() const;
				void SetFormDisabled(bool Disable);
				Rml::Element* GetElement() const;
				bool IsValid() const;
			};

			class TH_OUT IElementDocument : public IElement
			{
			public:
				IElementDocument(Rml::ElementDocument* Ref);
				virtual void Release() override;
				void SetTitle(const std::string& Title);
				void PullToFront();
				void PushToBack();
				void Show(ModalFlag Modal = ModalFlag_None, FocusFlag Focus = FocusFlag_Auto);
				void Hide();
				void Close();
				std::string GetTitle() const;
				std::string GetSourceURL() const;
				IElement CreateElement(const std::string& Name);
				bool IsModal() const;
				Rml::ElementDocument* GetElementDocument() const;
			};

			class TH_OUT Subsystem
			{
				friend DocumentSubsystem;
				friend ListenerSubsystem;
				friend DataSource;
				friend Context;

			private:
				static std::unordered_map<std::string, DataSource*>* Sources;
				static Script::VMManager* ScriptInterface;
				static ContextInstancer* ContextFactory;
				static DocumentInstancer* DocumentFactory;
				static ListenerInstancer* ListenerFactory;
				static RenderSubsystem* RenderInterface;
				static FileSubsystem* FileInterface;
				static MainSubsystem* SystemInterface;
				static uint64_t Id;
				static bool HasDecorators;
				static int State;

			public:
				static bool Create();
				static bool Release();
				static void SetMetadata(Graphics::Activity* Activity, ContentManager* Content, Rest::Timer* Time);
				static void SetTranslator(const std::string& Name, const TranslationCallback& Callback);
				static void SetManager(Script::VMManager* Manager);
				static RenderSubsystem* GetRenderInterface();
				static FileSubsystem* GetFileInterface();
				static MainSubsystem* GetSystemInterface();
				static Graphics::GraphicsDevice* GetDevice();
				static Graphics::Texture2D* GetBackground();
				static Compute::Matrix4x4* GetTransform();
				static Compute::Matrix4x4* GetProjection();
				static Compute::Matrix4x4 ToMatrix(const void* Matrix);
				static std::string EscapeHTML(const std::string& Text);

			private:
				static void ResizeDecorators();
				static void CreateDecorators(Graphics::GraphicsDevice* Device);
				static void ReleaseDecorators();
			};

			class TH_OUT DataNode
			{
				friend Context;
				friend DataModel;

			private:
				std::vector<DataNode> Childs;
				Compute::Property* Ref;
				DataModel* Handle;
				std::string* Name;
				bool Safe;

			private:
				DataNode(DataModel* Model, std::string* TopName, const Compute::Property& Initial);
				DataNode(DataModel* Model, std::string* TopName, Compute::Property* Reference);

			public:
				DataNode(const DataNode& Other);
				~DataNode();
				DataNode& Add(const Compute::PropertyList& Initial);
				DataNode& Add(const Compute::Property& Initial);
				DataNode& Add(Compute::Property* Reference);
				DataNode& At(size_t Index);
				size_t GetSize();
				bool Remove(size_t Index);
				bool Clear();
				void Set(const Compute::Property& NewValue);
				void Set(Compute::Property* NewReference);
				void SetString(const std::string& Value);
				void SetVector2(const Compute::Vector2& Value);
				void SetVector3(const Compute::Vector3& Value);
				void SetVector4(const Compute::Vector4& Value);
				void SetInteger(int64_t Value);
				void SetFloat(float Value);
				void SetDouble(double Value);
				void SetBoolean(bool Value);
				void SetPointer(void* Value);
				const Compute::Property& Get();
				std::string GetString();
				Compute::Vector2 GetVector2();
				Compute::Vector3 GetVector3();
				Compute::Vector4 GetVector4();
				int64_t GetInteger();
				float GetFloat();
				double GetDouble();
				bool GetBoolean();
				void* GetPointer();
				DataNode& operator= (const DataNode& Other);

			private:
				void GetValue(Rml::Variant& Result);
				void SetValue(const Rml::Variant& Result);
				void GetValueSize(Rml::Variant& Result);
			};

			class TH_OUT DataRow
			{
				friend DataSourceSubsystem;
				friend DataSource;

			private:
				std::vector<DataRow*> Childs;
				std::string Name;
				DataSource* Base;
				DataRow* Parent;
				void* Target;
				int Depth;

			private:
				DataRow(DataRow* NewParent, void* NewTarget);
				DataRow(DataSource* NewBase, void* NewTarget);

			public:
				~DataRow();
				DataRow* AddChild(void* Target);
				DataRow* GetChild(void* Target);
				DataRow* GetChildByIndex(size_t Index);
				DataRow* GetParent();
				DataRow* FindChild(void* Target);
				size_t GetChildsCount();
				bool RemoveChild(void* Target);
				bool RemoveChildByIndex(size_t Index);
				bool RemoveChilds();
				void SetTarget(void* NewTarget);
				void Update();

			public:
				template <typename T>
				T* GetTarget()
				{
					return (T*)Target;
				}
			};
			
			class TH_OUT DataModel : public Rest::Object
			{
				friend Context;

			private:
				std::unordered_map<std::string, DataNode*> Props;
				Rml::DataModelConstructor* Base;
				ModelCallback OnUnmount;

			private:
				DataModel(Rml::DataModelConstructor* Ref);

			public:
				virtual ~DataModel() override;
				DataNode* SetProperty(const std::string& Name, const Compute::Property& Value);
				DataNode* SetProperty(const std::string& Name, Compute::Property* Reference);
				DataNode* SetArray(const std::string& Name);
				DataNode* SetString(const std::string& Name, const std::string& Value);
				DataNode* SetInteger(const std::string& Name, int64_t Value);
				DataNode* SetFloat(const std::string& Name, float Value);
				DataNode* SetDouble(const std::string& Name, double Value);
				DataNode* SetBoolean(const std::string& Name, bool Value);
				DataNode* SetPointer(const std::string& Name, void* Value);
				DataNode* GetProperty(const std::string& Name);
				std::string GetString(const std::string& Name);
				int64_t GetInteger(const std::string& Name);
				float GetFloat(const std::string& Name);
				double GetDouble(const std::string& Name);
				bool GetBoolean(const std::string& Name);
				void* GetPointer(const std::string& Name);
				bool SetCallback(const std::string& Name, const DataCallback& Callback);
				bool SetUnmountCallback(const ModelCallback& Callback);
				bool HasChanged(const std::string& VariableName) const;
				void Change(const std::string& VariableName);
				bool IsValid() const;
			};

			class TH_OUT DataSource : public Rest::Object
			{
				friend DataSourceSubsystem;
				friend DataFormatterSubsystem;
				friend DataRow;
				friend Context;

			private:
				std::unordered_map<std::string, DataRow*> Nodes;
				DataFormatterSubsystem* DFS;
				DataSourceSubsystem* DSS;
				FormatCallback OnFormat;
				ColumnCallback OnColumn;
				ChangeCallback OnChange;
				DestroyCallback OnDestroy;
				std::string Name;
				DataRow* Root;

			private:
				DataSource(const std::string& NewName);

			public:
				virtual ~DataSource() override;
				void SetFormatCallback(const FormatCallback& Callback);
				void SetColumnCallback(const ColumnCallback& Callback);
				void SetChangeCallback(const ChangeCallback& Callback);
				void SetDestroyCallback(const DestroyCallback& Callback);
				void SetTarget(void* OldTarget, void* NewTarget);
				void Update(void* Target);
				DataRow* Get();

			protected:
				void RowAdd(const std::string& Table, int FirstRowAdded, int NumRowsAdded);
				void RowRemove(const std::string& Table, int FirstRowRemoved, int NumRowsRemoved);
				void RowChange(const std::string& Table, int FirstRowChanged, int NumRowsChanged);
				void RowChange(const std::string& Table);
			};

			class TH_OUT Handler : public Rest::Object
			{
				friend IElement;
				friend Context;

			private:
				Rml::EventListener* Base;

			public:
				Handler(const EventCallback& NewCallback);
				Handler(const std::string& FunctionName);
				virtual ~Handler() override;
			};

			class TH_OUT Context : public Rest::Object
			{
				friend DocumentSubsystem;
				friend ListenerSubsystem;

			private:
				struct
				{
					bool Keys = false;
					bool Scroll = false;
					bool Text = false;
					bool Cursor = false;
				} Inputs;

			private:
				std::unordered_map<int, std::unordered_map<std::string, Rml::Element*>> Elements;
				std::unordered_map<std::string, DataSource*> Sources;
				std::unordered_map<std::string, DataModel*> Models;
				Script::VMCompiler* Compiler;
				Compute::Vector2 Cursor;
				ModelCallback OnMount;
				ScopedContext* Base;
				bool Loading;

			public:
				Context(const Compute::Vector2& Size);
				Context(Graphics::GraphicsDevice* Device);
				virtual ~Context() override;
				void EmitKey(Graphics::KeyCode Key, Graphics::KeyMod Mod, int Virtual, int Repeat, bool Pressed);
				void EmitInput(const char* Buffer, int Length);
				void EmitWheel(int X, int Y, bool Normal, Graphics::KeyMod Mod);
				void EmitResize(int Width, int Height);
				void UpdateEvents(Graphics::Activity* Activity);
				void RenderLists(Graphics::Texture2D* RenderTarget);
				void ClearCache();
				IElementDocument Construct(const std::string& Path);
				bool Deconstruct();
				bool Inject(const std::string& ConfPath);
				bool AddFontFace(const std::string& Path, bool UseAsFallback = false);
				bool IsLoading();
				bool IsInputFocused();
				bool WasInputUsed(uint32_t InputTypeMask);
				const std::unordered_map<std::string, bool>& GetFontFaces();
				Rml::Context* GetContext();
				Compute::Vector2 GetDimensions() const;
				void SetDensityIndependentPixelRatio(float DensityIndependentPixelRatio);
				float GetDensityIndependentPixelRatio() const;
				IElementDocument CreateDocument(const std::string& InstancerName = "body");
				void EnableMouseCursor(bool Enable);
				IElementDocument GetDocument(const std::string& Id);
				IElementDocument GetDocument(int Index);
				int GetNumDocuments() const;
				IElement GetElementById(int DocIndex, const std::string& Id);
				IElement GetHoverElement();
				IElement GetFocusElement();
				IElement GetRootElement();
				IElement GetElementAtPoint(const Compute::Vector2& Point, const IElement& IgnoreElement = nullptr, const IElement& Element = nullptr) const;
				void PullDocumentToFront(const IElementDocument& Document);
				void PushDocumentToBack(const IElementDocument& Document);
				void UnfocusDocument(const IElementDocument& Document);
				void AddEventListener(const std::string& Event, Handler* Listener, bool InCapturePhase = false);
				void RemoveEventListener(const std::string& Event, Handler* Listener, bool InCapturePhase = false);
				bool IsMouseInteracting() const;
				bool GetActiveClipRegion(Compute::Vector2& Origin, Compute::Vector2& Dimensions) const;
				void SetActiveClipRegion(const Compute::Vector2& Origin, const Compute::Vector2& Dimensions);
				DataModel* SetDataModel(const std::string& Name);
				DataModel* GetDataModel(const std::string& Name);
				DataSource* SetDataSource(const std::string& Name);
				DataSource* GetDataSource(const std::string& Name);
				bool RemoveDataModel(const std::string& Name);
				bool RemoveDataModels();
				void SetDocumentsBaseTag(const std::string& Tag);
				void SetMountCallback(const ModelCallback& Callback);
				const std::string& GetDocumentsBaseTag();

			private:
				bool Inject(Rest::Document* Conf, const std::string& Relative);
				bool Preprocess(const std::string& Path, std::string& Buffer);
				void Decompose(std::string& Buffer);
				void CreateVM();
				void ClearVM();

			private:
				static int GetKeyCode(Graphics::KeyCode Key);
				static int GetKeyMod(Graphics::KeyMod Mod);
			};
		}
	}
}
#endif