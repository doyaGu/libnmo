#include "test_framework.h"

#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"

#include <stdio.h>

static void assert_lua_ok(nmo_lua_runtime_t *runtime, const char *script)
{
    nmo_status_t status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        if (message != NULL) {
            fprintf(stderr, "Lua failure: %s\n", message);
        }
    }
    ASSERT_EQ(NMO_OK, status);
}

#define OPEN_GAMEPLAY_MODEL                                                         \
    "local context = require('nmo.context')\n"                                      \
    "local document = require('nmo.document')\n"                                    \
    "local workspace_mod = require('nmo.workspace')\n"                              \
    "local script = require('nmo.script')\n"                                        \
    "local ctx = context.create()\n"                                                \
    "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo") "')\n" \
    "local m = script.model(workspace_mod.create(ctx, doc))\n"

TEST(lua_bindings_script, model_tables_refer_to_each_other)
{
    TEST_REQUIRE_FIXTURE("Ballance/Gameplay.nmo");
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        OPEN_GAMEPLAY_MODEL
        "assert(#m.nodes > 0 and #m.roots > 0)\n"
        /* Send Message #495 in Trafo Manager #877 of Gameplay_Ingame #3128 */
        "local send = m:get(495)\n"
        "assert(send.kind == 'BB' and send.parent.id == 877 and send.root.id == 3128)\n"
        "assert(send.root.depth == 0 and send.root.parent == nil)\n"
        "assert(tostring(send) == send.label)\n"
        "assert(send:path():find('/', 1, true))\n"
        "local found_child = false\n"
        "for _, child in ipairs(send.parent.children) do\n"
        "  if child == send then found_child = true end\n"
        "end\n"
        "assert(found_child)\n"
        /* its Message input reads a saved message */
        "local message = send:pin('Message')\n"
        "assert(message.owner == send and message.role == 'pIn')\n"
        "local value, how, origin = message:value()\n"
        "assert(value == 'BallNav deactivate' and how == 'saved')\n"
        "assert(origin.data == value and send:value('Message') == value)\n"
        "assert(message:describe() == '\"BallNav deactivate\"')\n"
        /* the index's uses hang off the node */
        "assert(#send.uses == 1)\n"
        "local use = send.uses[1]\n"
        "assert(use.kind == 'send' and use.message == 'BallNav deactivate')\n"
        "assert(use.dest.id == 10713 and use.root == send.root)\n"
        "assert(use:key() == 'BallNav deactivate')\n"
        /* every link joins IOs of nodes, and both ends list it */
        "for _, link in ipairs(m.links) do\n"
        "  assert(link.source.node ~= nil and link.target.node ~= nil)\n"
        "  local listed = false\n"
        "  for _, l in ipairs(link.source.links) do if l == link then listed = true end end\n"
        "  assert(listed)\n"
        "  listed = false\n"
        "  for _, l in ipairs(link.target.incoming) do if l == link then listed = true end end\n"
        "  assert(listed)\n"
        "end\n"
        /* every read edge's target is an input reading its source */
        "for _, edge in ipairs(m.edges) do\n"
        "  if edge.kind == 'read' then assert(edge.target.source == edge.source) end\n"
        "end\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_script, model_groups_uses_and_finds_nodes)
{
    TEST_REQUIRE_FIXTURE("Ballance/Gameplay.nmo");
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        OPEN_GAMEPLAY_MODEL
        "local bus\n"
        "for _, message in ipairs(m:messages()) do\n"
        "  if message.name == 'BallNav deactivate' then bus = message end\n"
        "end\n"
        "assert(bus ~= nil and #bus.send >= 1)\n"
        /* Get Cell #989 reads column Ball_Pos_Frame of CurrentLevel #10703 */
        "local level\n"
        "for _, array in ipairs(m:arrays()) do\n"
        "  if array.key == m:get(10703) then level = array end\n"
        "end\n"
        "assert(level ~= nil and level.name == 'CurrentLevel' and #level.read > 0)\n"
        "assert(m:get(10703).class ~= nil)\n"
        /* Activate Script #1094 activates Gameplay_Energy #4969 */
        "local energy\n"
        "for _, s in ipairs(m:scripts()) do\n"
        "  if s.key == m:get(4969) then energy = s end\n"
        "end\n"
        "assert(energy ~= nil and energy.name == 'Gameplay_Energy')\n"
        "local by_1094 = false\n"
        "for _, use in ipairs(energy.activate) do\n"
        "  if use.node.id == 1094 then by_1094 = true end\n"
        "end\n"
        "assert(by_1094)\n"
        /* find takes values, lists of values, and predicates */
        "local sends = m:find{proto_guid = m:get(495).proto_guid}\n"
        "assert(#sends > 1)\n"
        "for _, n in ipairs(sends) do assert(n.kind == 'BB') end\n"
        "local bbs = m:find{kind = {'BB'}, depth = function(d) return d >= 1 end}\n"
        "assert(#bbs > #sends)\n"
        "local under = m:get(3128):find{kind = 'BB'}\n"
        "for _, n in ipairs(under) do assert(n.root.id == 3128) end\n"
        "assert(#m:find_uses{kind = 'send'} > 0)\n"
        /* scripts can add methods to the classes */
        "local script = require('nmo.script')\n"
        "function script.Node:twice() return self.id * 2 end\n"
        "assert(m:get(495):twice() == 990)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_script, join_routes_messages_between_files)
{
    TEST_REQUIRE_FIXTURE("Ballance/Gameplay.nmo");
    TEST_REQUIRE_FIXTURE("Ballance/Sound.nmo");
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        OPEN_GAMEPLAY_MODEL
        "local sound_doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Sound.nmo") "')\n"
        "local sound = script.model(workspace_mod.create(ctx, sound_doc))\n"
        "local set = script.join{m, sound}\n"
        "local function use_of(model, id) return model:get(id).uses[1] end\n"
        /* the uses know their model */
        "assert(use_of(m, 845).model == m and m:get(845).model == m)\n"
        /* Wait Message #574 of Sound_Manager waits on the group All_Sound */
        "local sound_wait = use_of(sound, 574)\n"
        "assert(sound_wait.kind == 'wait' and sound_wait.route == 'object')\n"
        "local all_sound = sound_wait.listener\n"
        "assert(all_sound.name == 'All_Sound' and all_sound.class == 'CKGroup')\n"
        "assert(all_sound.classes.CKGroup and all_sound.classes.CKBeObject)\n"
        "assert(type(all_sound.members) == 'table')\n"
        "assert(set:object('All_Sound', 'CKGroup') == all_sound)\n"
        /* Send Message #845 sends "BallNav activate" to All_Sound, found by name in Sound.nmo */
        "local to_sound = use_of(m, 845)\n"
        "assert(to_sound.dest == nil and to_sound.route_name == 'All_Sound')\n"
        "assert(to_sound:reaches(sound_wait) == true)\n"
        /* Send Message #584 sends it to All_Gameplay: not to Sound_Manager */
        "local to_gameplay = use_of(m, 584)\n"
        "assert(to_gameplay:reaches(sound_wait) == false)\n"
        "assert(to_gameplay:reaches(use_of(m, 3074)) == true)\n"
        "local sure, maybe = set:receivers(to_sound)\n"
        "assert(#sure == 1 and sure[1] == sound_wait and #maybe == 0)\n"
        /* a group send reaches the group's own scripts */
        "local thunder = use_of(m, 10664)\n"
        "assert(thunder.route == 'group')\n"
        "local donner\n"
        "for _, use in ipairs(sound:get(698).uses) do\n"
        "  if use.message == 'Donner' then donner = use end\n"
        "end\n"
        "assert(thunder:reaches(donner, set) == true)\n"
        "local senders = set:senders(donner)\n"
        "assert(#senders == 1 and senders[1] == thunder)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_script, model_tables_refer_to_each_other);
    REGISTER_TEST(lua_bindings_script, model_groups_uses_and_finds_nodes);
    REGISTER_TEST(lua_bindings_script, join_routes_messages_between_files);
TEST_MAIN_END()
