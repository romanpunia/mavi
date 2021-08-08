#include "gui-lib.h"
#ifndef ANGELSCRIPT_H 
#include <angelscript.h>
#endif

namespace Tomahawk
{
	namespace Script
	{
		bool GUIRegisterElement(VMManager* Engine)
		{
#ifdef TH_WITH_RMLUI
			TH_ASSERT(Engine != nullptr, false, "manager should be set");
			VMGlobal& Register = Engine->Global();

			VMTypeClass VElement = Register.SetStructUnmanaged<Engine::GUI::IElement>("GuiElement");

			return true;
#else
			return false;
#endif
		}
		bool GUIRegisterDocument(VMManager* Engine)
		{
#ifdef TH_WITH_RMLUI
			TH_ASSERT(Engine != nullptr, false, "manager should be set");
			VMGlobal& Register = Engine->Global();

			VMTypeClass VDocument = Register.SetStructUnmanaged<Engine::GUI::IElementDocument>("GuiDocument");

			return true;
#else
			return false;
#endif
		}
		bool GUIRegisterEvent(VMManager* Engine)
		{
#ifdef TH_WITH_RMLUI
			TH_ASSERT(Engine != nullptr, false, "manager should be set");
			VMGlobal& Register = Engine->Global();

			VMTypeClass VEvent = Register.SetStructUnmanaged<Engine::GUI::IEvent>("GuiEvent");

			return true;
#else
			return false;
#endif
		}
		bool GUIRegisterContext(VMManager* Engine)
		{
#ifdef TH_WITH_RMLUI
			TH_ASSERT(Engine != nullptr, false, "manager should be set");
			VMGlobal& Register = Engine->Global();

			VMRefClass VContext = Register.SetClassUnmanaged<Engine::GUI::Context>("GuiContext");

			return true;
#else
			return false;
#endif
		}
	}
}