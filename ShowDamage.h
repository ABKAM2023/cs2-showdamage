#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "igameevents.h"
#include "vector.h"
#include <deque>
#include <functional>
#include <utlstring.h>
#include <KeyValues.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "include/menus.h"
#include "include/cookies.h"

class ShowDamage final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	void AllPluginsLoaded();
private:
	struct GrenadeAggregate
	{
		int totalDamage{0};
		std::unordered_set<int> victims;
		float lastUpdate{0.0f};
		bool scheduled{false};
		CTimer* timer{nullptr};
		bool isInferno{false};
	};

	struct HudState
	{
		CTimer* timer{nullptr};
		float endTime{0.0f};
		std::string html;
		int durationSec{0};
	};

	void RegisterEvents();
	void OnPlayerHurt(IGameEvent* pEvent);
	void DispatchGrenade(int attackerSlot);
	void StopGrenadeTimer(int attackerSlot);
	void SendHudMessage(int slot, const std::string& html, int durationSec);
	void StopHudTimer(int slot);
	void ApplyDamagePreference(int slot, bool enabled);
	void LoadTranslations();
	static std::string Phrase(const char* id, const char* fallback = "");
	static std::string HitgroupLabel(int hitgroup);
	static bool IsGrenadeWeapon(const char* weapon);
	static bool IsInfernoWeapon(const char* weapon);

	std::unordered_map<int, GrenadeAggregate> m_GrenadeAggregates;
	std::unordered_map<int, HudState> m_HudStates;
	bool m_ShowDamageEnabled[64]{};

	const char* GetAuthor();
	const char* GetName();
	const char* GetDescription();
	const char* GetURL();
	const char* GetLicense();
	const char* GetVersion();
	const char* GetDate();
	const char* GetLogTag();
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
