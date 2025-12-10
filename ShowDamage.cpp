#include <stdio.h>
#include "ShowDamage.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <algorithm>
#include <cstdlib>

ShowDamage g_ShowDamage;
PLUGIN_EXPOSE(ShowDamage, g_ShowDamage);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi* g_pUtils;
IPlayersApi* g_pPlayers;
ICookiesApi* g_pCookies;

bool g_bShowBotDamage = true;

static std::string g_consoleCommand = "mm_damage";
static std::vector<std::string> g_chatCommands = {"!damage", "!dmg"};

static std::unordered_map<std::string, std::string> g_vecPhrases;

static constexpr const char* kDamageCookieName = "ShowDamage.Enabled";

static int MapUserToSlot(int id)
{
	if (id < 0)
	{
		return -1;
	}

	if (g_pPlayers && g_pPlayers->IsInGame(id))
	{
		return id;
	}

	if (g_pPlayers && id > 0 && g_pPlayers->IsInGame(id - 1))
	{
		return id - 1;
	}

	return id;
}

static std::string Trim(const std::string& str)
{
	const size_t first = str.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return std::string();
	}
	const size_t last = str.find_last_not_of(" \t\r\n");
	return str.substr(first, last - first + 1);
}

static std::vector<std::string> Split(const std::string& s, char delim)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char c : s)
	{
		if (c == delim)
		{
			std::string trimmed = Trim(cur);
			if (!trimmed.empty())
			{
				parts.push_back(trimmed);
			}
			cur.clear();
		}
		else
		{
			cur.push_back(c);
		}
	}
	std::string trimmed = Trim(cur);
	if (!trimmed.empty())
	{
		parts.push_back(trimmed);
	}
	return parts;
}

static std::string NormalizeHtmlNewlines(std::string s)
{
	size_t pos = 0;
	const std::string from = "\\n";
	const std::string to = "<br>";
	while ((pos = s.find(from, pos)) != std::string::npos)
	{
		s.replace(pos, from.length(), to);
		pos += to.length();
	}
	return s;
}

CGameEntitySystem* GameEntitySystem() 
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool ShowDamage::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	
	g_SMAPI->AddListener( this, this );

	return true;
}

bool ShowDamage::Unload(char *error, size_t maxlen)
{
	if (g_pUtils)
	{
		g_pUtils->ClearAllHooks(g_PLID);

		for (auto &pair : m_HudStates)
		{
			if (pair.second.timer)
			{
				g_pUtils->RemoveTimer(pair.second.timer);
				pair.second.timer = nullptr;
			}
		}
		m_HudStates.clear();

		for (auto &pair : m_GrenadeAggregates)
		{
			if (pair.second.timer)
			{
				g_pUtils->RemoveTimer(pair.second.timer);
				pair.second.timer = nullptr;
			}
		}
		m_GrenadeAggregates.clear();
	}
	
	return true;
}

static void LoadConfig();

void ShowDamage::AllPluginsLoaded()
{
	char error[64];
	int ret;
	for (int i = 0; i < 64; ++i)
	{
		m_ShowDamageEnabled[i] = true;
	}
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pUtils->StartupServer(g_PLID, StartupServer);
		LoadTranslations();
		LoadConfig();

	g_pPlayers = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	g_pCookies = (ICookiesApi*)g_SMAPI->MetaFactory(COOKIES_INTERFACE, &ret, NULL);
	if (g_pCookies && ret != META_IFACE_FAILED)
	{
		g_pCookies->HookClientCookieLoaded(g_PLID, [this](int slot)
		{
			if (slot < 0 || slot >= 64)
			{
				return;
			}
			bool enabled = true;
			const char* val = g_pCookies->GetCookie(slot, kDamageCookieName);
			if (val && *val)
			{
				enabled = std::atoi(val) != 0;
			}
			ApplyDamagePreference(slot, enabled);
		});
	}

	if (g_pUtils)
	{
		std::vector<std::string> consoleCmds;
		if (!g_consoleCommand.empty())
		{
			consoleCmds.push_back(g_consoleCommand);
		}
		else
		{
			consoleCmds.push_back("mm_damage");
		}

		std::vector<std::string> chatCmds = g_chatCommands.empty() ? std::vector<std::string>{"!damage", "!dmg"} : g_chatCommands;

		g_pUtils->RegCommand(g_PLID, consoleCmds, chatCmds, [this](int iSlot, const char* /*szContent*/)
		{
			if (iSlot < 0 || iSlot >= 64)
			{
				return false;
			}
			bool newState = !m_ShowDamageEnabled[iSlot];
			ApplyDamagePreference(iSlot, newState);
			if (g_pUtils)
			{
				const char* chatId = newState ? "Chat_DamageOn" : "Chat_DamageOff";
				std::string msg = Phrase(chatId, newState ? "Damage display: ON" : "Damage display: OFF");
				std::string chatMsg = " " + msg;
				g_pUtils->PrintToChat(iSlot, chatMsg.c_str());
			}
			return false;
		});
	}
	g_pUtils->HookEvent(g_PLID, "player_hurt", [this](const char* /*szName*/, IGameEvent* pEvent, bool /*bDontBroadcast*/)
	{
		OnPlayerHurt(pEvent);
	});
}

void ShowDamage::OnPlayerHurt(IGameEvent* pEvent)
{
	if (!pEvent || !g_pUtils)
	{
		return;
	}

	const int attackerSlot = pEvent->GetInt("attacker", 0);
	const int victimSlot = pEvent->GetInt("userid", 0);
	const int damage = pEvent->GetInt("dmg_health", 0);
	const int remainingHp = pEvent->GetInt("health", 0);
	const int hitgroup = pEvent->GetInt("hitgroup", 0);
	const char* weapon = pEvent->GetString("weapon", "");

	const int attackerIndex = MapUserToSlot(attackerSlot);
	const int victimIndex = MapUserToSlot(victimSlot);

	if (attackerIndex < 0 || damage <= 0)
	{
		return;
	}

	if (attackerIndex >= 0 && attackerIndex < 64 && !m_ShowDamageEnabled[attackerIndex])
	{
		return;
	}

	const bool victimIsBot = (victimIndex >= 0) && g_pPlayers && g_pPlayers->IsFakeClient(victimIndex);
	if (victimIsBot && !g_bShowBotDamage)
	{
		return;
	}

	const float curtime = gpGlobals ? gpGlobals->curtime : 0.0f;

	const bool weaponIsInferno = IsInfernoWeapon(weapon);
	if (IsGrenadeWeapon(weapon) || weaponIsInferno)
	{
		GrenadeAggregate& agg = m_GrenadeAggregates[attackerIndex];
		const bool isInferno = weaponIsInferno;
		const bool wasInferno = agg.isInferno;

		const float resetWindow = isInferno ? 2.0f : 1.0f;
		if (curtime - agg.lastUpdate > resetWindow)
		{
			agg = GrenadeAggregate{};
		}

		if (isInferno && agg.timer && !wasInferno && g_pUtils)
		{
			g_pUtils->RemoveTimer(agg.timer);
			agg.timer = nullptr;
		}
		else if (!isInferno && agg.timer && wasInferno && g_pUtils)
		{
			g_pUtils->RemoveTimer(agg.timer);
			agg.timer = nullptr;
		}

		agg.isInferno = isInferno;

		agg.totalDamage += damage;
		const int victimId = (victimIndex >= 0) ? victimIndex : victimSlot;
		agg.victims.insert(victimId);
		agg.lastUpdate = curtime;

		if (isInferno)
		{
			char fireBuf[512];
			std::string tmpl = NormalizeHtmlNewlines(Phrase(
				"HTML_Fire",
				"Общий урон от огня: <font color='#ff9900'>%d</font> <img src='https://i.ibb.co/DfQNhqfC/image-2025-12-10-20-25-52.png' />\nЗадето игроков: <font color='yellow'>%zu</font>"));
			g_SMAPI->Format(fireBuf, sizeof(fireBuf), tmpl.c_str(), agg.totalDamage, agg.victims.size());
			SendHudMessage(attackerIndex, fireBuf, 2);

			if (!agg.timer)
			{
				agg.timer = g_pUtils->CreateTimer(0.5f, [this, attackerIndex]() -> float
				{
					if (!g_pUtils)
					{
						return -1.0f;
					}
					auto it = m_GrenadeAggregates.find(attackerIndex);
					if (it == m_GrenadeAggregates.end())
					{
						return -1.0f;
					}
					GrenadeAggregate &ag = it->second;
					const float nowCheck = gpGlobals ? gpGlobals->curtime : 0.0f;
					if (nowCheck - ag.lastUpdate > 2.0f)
					{
						ag.timer = nullptr;
						m_GrenadeAggregates.erase(attackerIndex);
						return -1.0f;
					}
					return 0.5f;
				});
			}
			return;
		}

		if (!agg.timer)
		{
			agg.timer = g_pUtils->CreateTimer(0.2f, [this, attackerIndex]() -> float
			{
				if (!g_pUtils)
				{
					return -1.0f;
				}
				auto it = m_GrenadeAggregates.find(attackerIndex);
				if (it == m_GrenadeAggregates.end())
				{
					return -1.0f;
				}
				GrenadeAggregate &ag = it->second;
				if (ag.isInferno)
				{
					ag.timer = nullptr;
					m_GrenadeAggregates.erase(it);
					return -1.0f;
				}
				const float nowCheck = gpGlobals ? gpGlobals->curtime : 0.0f;
				if (nowCheck - ag.lastUpdate > 0.3f)
				{
					DispatchGrenade(attackerIndex);
					ag.timer = nullptr;
					return -1.0f;
				}
				return 0.2f;
			});
		}

		return;
	}

	char buffer[512];
	std::string hitTmpl = NormalizeHtmlNewlines(Phrase(
		"HTML_Hit",
		"Урон: <font color='yellow'>%d</font> <img src='https://i.ibb.co/VJMw9Gd/image.png' />\nОстаток HP: <font color='red'>%d</font> <img src='https://i.ibb.co/3FDnCmq/image.png' />\nПопадание: <font color='green'>%s</font>"));
	std::string hitLabel = HitgroupLabel(hitgroup);
	g_SMAPI->Format(buffer, sizeof(buffer), hitTmpl.c_str(), damage, remainingHp, hitLabel.c_str());

	SendHudMessage(attackerIndex, buffer, 5);
}

void ShowDamage::DispatchGrenade(int attackerSlot)
{
	auto it = m_GrenadeAggregates.find(attackerSlot);
	if (it == m_GrenadeAggregates.end())
	{
		return;
	}

	GrenadeAggregate agg = it->second;
		if (agg.isInferno)
		{
			m_GrenadeAggregates.erase(it);
			return;
		}
	m_GrenadeAggregates.erase(it);

	char buffer[512];
		std::string tmpl = NormalizeHtmlNewlines(Phrase(
			"HTML_Grenade",
			"Урон от гранаты: <font color='green'>%d</font> <img src='https://i.ibb.co/m9ZVjCD/image.png' />\nЗадето игроков: <font color='yellow'>%zu</font>"));
		g_SMAPI->Format(buffer, sizeof(buffer), tmpl.c_str(), agg.totalDamage, agg.victims.size());

	SendHudMessage(attackerSlot, buffer, 5);
}

void ShowDamage::StopGrenadeTimer(int attackerSlot)
{
	auto it = m_GrenadeAggregates.find(attackerSlot);
	if (it == m_GrenadeAggregates.end())
	{
		return;
	}
	if (it->second.timer && g_pUtils)
	{
		g_pUtils->RemoveTimer(it->second.timer);
	}
	m_GrenadeAggregates.erase(it);
}

void ShowDamage::SendHudMessage(int slot, const std::string& html, int durationSec)
{
	if (slot < 0)
	{
		return;
	}

	StopHudTimer(slot);

	HudState& state = m_HudStates[slot];
	state.html = html;
	state.durationSec = durationSec > 0 ? durationSec : 5;
	const float now = gpGlobals ? gpGlobals->curtime : 0.0f;
	state.endTime = now + static_cast<float>(state.durationSec);

	g_pUtils->PrintToCenterHtml(slot, state.durationSec, "%s", state.html.c_str());
	state.timer = g_pUtils->CreateTimer(0.0f, [this, slot]() -> float
	{
		if (!g_pUtils)
		{
			return -1.0f;
		}
		auto it = m_HudStates.find(slot);
		if (it == m_HudStates.end())
		{
			return -1.0f;
		}
		HudState &st = it->second;
		const float nowInner = gpGlobals ? gpGlobals->curtime : 0.0f;
		if (nowInner >= st.endTime)
		{
			st.timer = nullptr;
			return -1.0f;
		}
		g_pUtils->PrintToCenterHtml(slot, st.durationSec, "%s", st.html.c_str());
		return 0.0f;
	});
}

void ShowDamage::StopHudTimer(int slot)
{
	auto it = m_HudStates.find(slot);
	if (it == m_HudStates.end())
	{
		return;
	}
	if (it->second.timer && g_pUtils)
	{
		g_pUtils->RemoveTimer(it->second.timer);
	}
	it->second.timer = nullptr;
	m_HudStates.erase(it);
}

void ShowDamage::ApplyDamagePreference(int slot, bool enabled)
{
	if (slot < 0 || slot >= 64)
	{
		return;
	}
	m_ShowDamageEnabled[slot] = enabled;
	if (!enabled)
	{
		StopHudTimer(slot);
		StopGrenadeTimer(slot);
	}
	if (g_pCookies)
	{
		g_pCookies->SetCookie(slot, kDamageCookieName, enabled ? "1" : "0");
	}
}

static void LoadConfig()
{
	g_consoleCommand = "mm_damage";
	g_chatCommands = {"!damage", "!dmg"};
	g_bShowBotDamage = true;

	if (!g_pFullFileSystem)
	{
		return;
	}

	KeyValues::AutoDelete kv("ShowDamage");
	const char* pszPath = "addons/configs/showdamage_settings.ini";
	if (!kv->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s, using defaults\n", pszPath);
		return;
	}

	const char* consoleCmd = kv->GetString("console_command", g_consoleCommand.c_str());
	if (consoleCmd && *consoleCmd)
	{
		g_consoleCommand = consoleCmd;
	}

	const char* chatCmds = kv->GetString("chat_commands", "");
	if (chatCmds && *chatCmds)
	{
		auto parts = Split(chatCmds, ',');
		if (!parts.empty())
		{
			g_chatCommands = parts;
		}
	}

	g_bShowBotDamage = kv->GetBool("show_bot_damage", g_bShowBotDamage);
}

void ShowDamage::LoadTranslations()
{
	g_vecPhrases.clear();
	if (!g_pUtils)
	{
		return;
	}
	if (!g_pFullFileSystem)
	{
		return;
	}
	KeyValues::AutoDelete kv("Phrases");
	const char *pszPath = "addons/translations/showdamage.phrases.txt";
	if (!kv->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return;
	}

	std::string lang = g_pUtils->GetLanguage();
	const char* pszLang = lang.c_str();
	for (KeyValues *pKey = kv->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
	{
		g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(pszLang));
	}
}

std::string ShowDamage::Phrase(const char* id, const char* fallback)
{
	auto it = g_vecPhrases.find(id ? id : "");
	if (it != g_vecPhrases.end() && !it->second.empty())
	{
		return it->second;
	}
	return fallback ? std::string(fallback) : std::string();
}

std::string ShowDamage::HitgroupLabel(int hitgroup)
{
	const char* key = nullptr;
	const char* fallback = nullptr;
	switch (hitgroup)
	{
		case 1: key = "Hit_Head"; fallback = "Голова"; break;
		case 2: key = "Hit_Chest"; fallback = "Грудь"; break;
		case 3: key = "Hit_Stomach"; fallback = "Живот"; break;
		case 4: key = "Hit_LeftArm"; fallback = "Левая рука"; break;
		case 5: key = "Hit_RightArm"; fallback = "Правая рука"; break;
		case 6: key = "Hit_LeftLeg"; fallback = "Левая нога"; break;
		case 7: key = "Hit_RightLeg"; fallback = "Правая нога"; break;
		default: key = "Hit_Generic"; fallback = "Тело"; break;
	}
	return Phrase(key, fallback);
}

bool ShowDamage::IsGrenadeWeapon(const char* weapon)
{
	if (!weapon)
	{
		return false;
	}

	std::string lower = weapon;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

	return lower.find("hegrenade") != std::string::npos;
}

bool ShowDamage::IsInfernoWeapon(const char* weapon)
{
	if (!weapon)
	{
		return false;
	}

	std::string lower = weapon;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

	return lower.find("inferno") != std::string::npos;
}

const char* ShowDamage::GetLicense()
{
	return "GPL";
}

const char* ShowDamage::GetVersion()
{
	return "1.0";
}

const char* ShowDamage::GetDate()
{
	return __DATE__;
}

const char *ShowDamage::GetLogTag()
{
	return "[ShowDamage]";
}

const char* ShowDamage::GetAuthor()
{
	return "ABKAM";
}

const char* ShowDamage::GetDescription()
{
	return "ShowDamage";
}

const char* ShowDamage::GetName()
{
	return "ShowDamage";
}

const char* ShowDamage::GetURL()
{
	return "https://discord.gg/ChYfTtrtmS";
}