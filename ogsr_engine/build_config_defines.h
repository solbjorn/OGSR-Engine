#pragma once

// Для изменения настроек нужно создать папку build_config_overrides,
// скопировать этот файл в нее и изменить, как требуется.

//***********************************[Настройки отладочных режимов]***********************************
// Включает вылет, если в vertex() будет передан не валидный вертекс
// #define CRASH_ON_INVALID_VERTEX_ID

// Отключить все отладочные ассерты DEBUG конфигурации
// Это может пригодиться в случае, если нужно отлаживать какой-нибудь мод, но отладочную сборку с этим модом запустить нереально.
// Из-за того, что в модах зачастую всё очень криво сделано, ассерты будут срабатывать на каждый чих. Это и на ОГСЕ так, на солянке - думаю, ещё хуже.
// В этом случае нужно переключиться на DEBUG конфигурацию, и раскомментировать этот дефайн:
#define DISABLE_DBG_ASSERTIONS

// Валидатор желательно использовать только при отладке проблем с памятью.
// #define USE_MEMORY_VALIDATOR
//****************************************************************************************************

#define FSLTX "fsgame.ltx"

// Сколько последних сообщений показывать в истории ПДА
#define NEWS_TO_SHOW 500

// Изменяет принцип работы дуплета для оружия на классе CWeaponShotgun
// Вместо дуплета по кнопке прицеливания, дуплет можно включить\выключить скриптом
// Вкдючить\выключить - wpn:get_weapon_sg():switch_duplet()
// Проверить статус - wpn:get_weapon_sg().is_duplet_enabled
// Сделано для того, что бы сохранить возможность прицеливания
// KRodin to all: Дуплет вообще надо вынести в отдельный режим стрельбы, и убрать эту опцию вообще.
// #define DUPLET_STATE_SWITCH

// Включает учет параметра k_ap у патрона при расчета урона для НПС
// Формула такая же как при расчете урона для ГГ
// Может потребовать перенстройку конфигов патронов
#define APPLY_ARMOR_PIERCING_TO_NPC

// В этом режиме удаление поршня, в котором указаны <article>, удалит и эти
// статьи.
// #define REMOVE_ARTICLES_ON_DISABLE_INFO

// Бывают ситуации когда надо переспавнить объект, у которого в оллспавне прописан стори_айди, но не прописан спавн_айди.
// К примеру alife():create( alife():spawn_id( story_ids.bar_dolg_captain_ivancov ) ). Вот включив эту опцию будет достаточно одного стори_айди.
// #define USE_STORY_ID_AS_SPAWN_ID

//****************************************************************************************************
// Включить поддержку textures.ltx, parallax из OGSE так же поддерживается
#define USE_TEXTURES_LTX
//****************************************************************************************************

// Включает поддержку анимированных физобъектов типа крутящегося вентилятора в подземке агропрома из ЧН.
#define ANIMATED_PHYSICS_OBJECT_SUPPORT

// Uncomment to enable Discord integration
#define XR_DISCORD
// Discord application ID
#define XR_DISCORD_APPID 1507428629104295938

// Discord GitHub button label
#define XR_DISCORD_GITHUB_LABEL "400 BPM GitHub"
// Discord GitHub button URL
#define XR_DISCORD_GITHUB_URL "https://github.com/solbjorn/reaper-engine"

// Discord server button label
#define XR_DISCORD_SERVER_LABEL "OGSR Discord"
// Discord server button URL
#define XR_DISCORD_SERVER_URL "https://discord.com/servers/ogsr-508530704674455562"

// Uncomment to use {fmt} instead of std::format
#define XR_USE_FMT

// Uncomment to disable ImGui and the ingame editor completely
// #define IMGUI_DISABLE
// Uncomment to enable ImGui exports to Lua
#define XR_IMGUI_LUA

// Enable Sentry + Crashpad crash backend
#define XR_SENTRY
// Sentry cloud DSN
#define XR_SENTRY_DSN nullptr

// Enable RenderDoc integration (place a prebuilt renderdoc.dll to bin_x64/)
#define XR_USE_RENDERDOC
// Uncomment to enable profiling with Tracy
// #define TRACY_ENABLE

// Uncomment to enable runtime checks for the RTTI tree validity. Each result
// of smart_cast<>() will be rechecked against the result of dynamic_cast<>()
// to make sure every class has its own RTTI, not inherited from a parent.
// #define XR_RTTI_DEBUG

// Default password for encrypted [lib]archive (7-Zip, Rar) archives
#define XR_ARC_PASSWORD ""

// Default password for encrypted ZIP archives
#define XR_ZIP_PASSWORD ""
