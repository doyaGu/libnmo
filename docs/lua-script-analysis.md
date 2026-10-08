# Analyzing Behavior Scripts with Lua

The `nmo.script` Lua module turns the behaviors of a Virtools file into plain Lua tables you
can query: every script and graph, their building blocks, IOs, parameters and their values,
behavior links, parameter flows, and what each building block does with messages, data arrays,
and other scripts. `nmo script analyze` runs a Lua script over the models of one or more files
without changing them, and ships analyses of its own.

This guide covers running analyses, the tables a model is made of, their methods, and how to
analyze several files of one game together.

## Running an analysis

```
nmo script analyze <script.lua | analysis> <file>... [-- <arg>...]
nmo script analyze --list
```

The command opens each file read-only, builds its model, and runs the script once. In the
script:

| Global   | Value                                                               |
|----------|---------------------------------------------------------------------|
| `models` | One model per file, in the order given. `m.path` is the path as given, `m.name` its file name. |
| `model`  | `models[1]`.                                                        |
| `arg`    | The arguments after `--`, from `arg[1]`; `arg[0]` is the script. They are also the chunk's `...`. |

`print()` writes to the command output (`-o` redirects it). A Lua error stops the script and
prints the message with its file and line.

```lua
-- sends.lua: every Send Message block and the message it sends
for _, node in ipairs(model:find{ proto = "Send Message" }) do
    print(node:where(), (node:value("Message")))
end
```

```
$ nmo script analyze sends.lua Gameplay.nmo
Send Message#495 in Gameplay_Ingame#3128 / Trafo Manager#877	BallNav deactivate
Send Message#584 in Gameplay_Ingame#3128 / Trafo Manager#877	BallNav activate
...
```

### Bundled analyses

Name one of these instead of a script file. Their sources are in
[`tools/lua/analyze/`](../tools/lua/analyze/) and make good starting points for your own.

| Analysis       | What it reports                                                         |
|----------------|-------------------------------------------------------------------------|
| `summary`      | Per root: the messages it waits for and sends, the arrays (and columns) it reads and writes, the scripts it activates, the keys it reacts to. `-- <name>` keeps the roots whose name contains it. |
| `messages`     | Per message: its senders, the receivers each send reaches, and its receivers; then the sends no receiver gets and the receivers no send reaches. `-- <name>` filters by message name. |
| `arrays`       | Per data array, joined by name across the files: who reads and writes it, by column; then the arrays read but never written. `-- <name>` filters by array name. |
| `interactions` | Which scripts start which: messages that reach them, and script activation. `-- --dot` prints a Graphviz graph. |

```
nmo script analyze summary Gameplay.nmo -- energy
nmo script analyze messages data/Ballance/*.nmo data/Ballance/*.cmo
nmo script analyze interactions *.nmo base.cmo -- --dot > scripts.dot
```

The single-file views of the same information are `nmo script xref <file>` (messages, arrays,
and scripts with every use) and `nmo script view <id> <file>` (one graph as readable blocks).

### Outside the CLI

Any Lua runtime with the nmo bindings can build a model from a workspace:

```lua
local context = require("nmo.context")
local document = require("nmo.document")
local workspace = require("nmo.workspace")
local script = require("nmo.script")

local ctx = context.create{ data_dir = "path/to/data" }
local doc = document.load_file(ctx, "Gameplay.nmo")
local m = script.model(workspace.create(ctx, doc))
```

`data_dir` is the directory holding `virtools_building_blocks.json` and the other Virtools
data files the CLI finds by itself. Without it the model still builds, but building blocks
have no prototype or operation names (`proto` and `op` are nil): match them by `proto_guid`
instead.

The model is a snapshot. It copies everything it needs, so it stays valid after the document
is closed, and it does not see later edits.

From C, `nmo_lua_push_script_model(L, workspace)` (`lua/nmo_lua_script.h`) pushes the same
table, and `nmo_lua_runtime_execute_file()` / `nmo_lua_runtime_execute_buffer()` run a script
with arguments, as `nmo script analyze` does.

## The model

A model is a graph of tables. Items refer to each other directly (`node.parent`,
`param.source`, `link.target.node`, ...), and every item an object of the file stands for is
also in `m.by_id`, so `m:get(id)` finds it by object id.

| Field          | Contents                                                                |
|----------------|-------------------------------------------------------------------------|
| `nodes`        | Every node (script, graph, building block), in document order.          |
| `roots`        | The nodes no graph holds: the scripts, and the graphs of a behavior file. |
| `ios`, `params`, `operations`, `links`, `edges`, `uses` | Every item of that kind. |
| `by_id`        | Object id → node, IO, parameter, operation, or link.                    |
| `objects`      | Object id → object table, for the other objects the model refers to.    |
| `path`, `name` | Set by `nmo script analyze`: the file.                                  |

Every table prints usefully: `tostring(node)` is its label (`Key Event#1589`), a parameter
prints as `Owner#id.Name`, an IO as `Owner#id.Name`, a link as `source -> target`.

### Nodes

| Field             | Meaning                                                              |
|-------------------|----------------------------------------------------------------------|
| `id`, `name`      | Object id and name (`""` when unnamed).                              |
| `kind`            | `"Script"`, `"Graph"`, or `"BB"`.                                    |
| `label`           | `"Name#id"`; `"Name(Operation)#id"` for an Op building block.        |
| `parent`, `root`  | The graph holding it, and its root. A root's `parent` is nil and its `root` itself. |
| `depth`           | 0 for a root.                                                        |
| `owner`           | Roots: the object whose script it is.                                |
| `proto`, `proto_guid` | Building blocks: the prototype name (`"Send Message"`) and GUID (`"{A20E8D5B-DF002150}"`). |
| `op`, `op_guid`   | Op building blocks: the operation they run (`"Division"`).           |
| `flags`           | The `CKBEHAVIOR_*` flags.                                            |
| `children`        | Sub-behaviors, in document order.                                    |
| `inputs`, `outputs` | Behavior IOs (bIn, bOut).                                          |
| `target`          | The target parameter, when the behavior has one.                     |
| `pins`, `pouts`   | Input and output parameters.                                         |
| `locals`, `settings` | Local parameters; `settings` holds the locals that are building block settings. |
| `operations`, `links`, `edges` | Graphs: their parameter operations, behavior links, and parameter flows. |
| `uses`            | What the node does with messages, arrays, and scripts (see [Uses](#uses)). |
| `model`           | The model it belongs to.                                             |

| Method                    | Returns                                                       |
|---------------------------|---------------------------------------------------------------|
| `n:walk()`                | An iterator over the node and everything under it, depth first. |
| `n:find(criteria)`        | The nodes of the subtree matching the [criteria](#criteria).  |
| `n:path()`                | `"Root/Graph/Node"`, by names.                                |
| `n:where()`               | `"Node#id in Root#id / Graph#id"`.                            |
| `n:input(key)`, `n:output(key)` | The IO named `key`, or at position `key`.               |
| `n:pin(key)`, `n:pout(key)`, `n:var(key)`, `n:setting(key)` | The input, output, local, or setting parameter named (or at position) `key`. |
| `n:next(key)`             | The IOs the links leaving output `key` activate (every output when nil). |
| `n:prev(key)`             | The IOs whose links activate input `key` (every input when nil). |
| `n:value(key)`            | `n:pin(key):value()`.                                         |

```lua
local send = model:get(495)
for _, io in ipairs(send:prev()) do print("from", io) end   -- from set Piecesflag#797.Out 0
for _, io in ipairs(send:next()) do print("to", io) end     -- to Send Message#839.In
```

### IOs and links

An IO has `id`, `name`, `node`, `index` (1-based), `is_output`, `links` (the links leaving
it), and `incoming` (the links entering it). A link has `id`, `graph`, `source` and `target`
(IOs), `delay`, and `initial_delay`.

### Parameters

| Field             | Meaning                                                              |
|-------------------|----------------------------------------------------------------------|
| `id`, `name`      | Object id and name.                                                  |
| `role`            | `"target"`, `"pIn"`, `"pOut"`, `"local"`, `"operation pIn"`, `"operation pOut"`, or `"external"` for a parameter no behavior or operation holds. |
| `owner`           | The node or operation holding it.                                    |
| `index`           | 1-based position among its owner's parameters of that role.          |
| `type`, `type_guid` | The parameter type.                                                |
| `setting`         | A building block setting.                                            |
| `source`          | Inputs: the parameter they read; `shared` when that is another input. |
| `text`            | Its own saved value as text (`'"Up" (200)'`, `"2.5"`, `"#10703 (CurrentLevel)"`). |
| `data`            | Its own saved value as data (see below).                             |
| `origin`, `origin_kind` | The parameter its value comes from, and how (see below).       |
| `in_edges`, `out_edges` | The data edges into and out of it.                             |

Inputs hold no value of their own. To know what an input gets, follow it to its **origin**:
inputs are followed through their sources (shared inputs and graph inputs included) to a
local, a plain parameter, or an output. `origin_kind` says what that origin is:

| `origin_kind` | Meaning                                                                 |
|---------------|-------------------------------------------------------------------------|
| `"saved"`     | A local or parameter no output writes: its saved value is the value.    |
| `"written"`   | Outputs write it at run time: its saved value is only the one it starts with. |
| `"computed"`  | An output or an operation result: set at run time, nothing saved.       |
| `"none"`      | The input reads nothing.                                                |

Only the writes of the same file count: a local another file writes reads as `"saved"`.

`data` is a Lua value: a boolean, an integer (integers, enumerations, flags, keyboard keys), a
number, a string, a message name, an object (see [Objects](#objects)), or a list of numbers
(vectors, colors, matrices, boxes, ...). It is nil for values only `text` describes.

| Method          | Returns                                                                |
|-----------------|------------------------------------------------------------------------|
| `p:value()`     | The value it takes, how (`origin_kind`), and the origin. The value is the origin's `data` when saved or written, nil otherwise. |
| `p:describe()`  | That as text: `'"BallNav deactivate"'`, `'(none), written by Get Row#5079.up'`, `'computed by Op(Division)#1707.res'`, `'not connected'`. |
| `p:readers()`   | The inputs reading it.                                                 |
| `p:writes()`    | The parameters an output writes.                                       |
| `p:writers()`   | The outputs writing it.                                                |
| `p:node()`      | The node holding it, through the operation holding it.                 |

Ballance reads its key bindings from a data array at run time; the origins show it:

```lua
for _, node in ipairs(model:find{ proto = "Key Event" }) do
    local key = node:pin("Key Waited")
    local _, how, origin = key:value()
    if how == "written" then
        for _, writer in ipairs(origin:writers()) do
            for _, use in ipairs(writer:node().uses) do
                print(node.label, "<-", writer, "reads", use:key_text())
            end
        end
    else
        print(node.label, key:describe())
    end
end
```

```
Key Event#1589	<-	Get Row#5079.up	reads	"Keyboard"
Key Event#1597	<-	Get Row#5079.down	reads	"Keyboard"
Key Event#1682	"1" (2)
...
```

### Operations and data edges

A parameter operation has `id`, `name` (the operation, `"Division"`), `label`, `guid`,
`graph`, `in1`, `in2`, `out` (parameters; nil when absent), and `params`.

A data edge is one parameter flow inside a graph:

| Field                          | Meaning                                                  |
|--------------------------------|----------------------------------------------------------|
| `graph`                        | The graph it is in.                                      |
| `kind`                         | `"read"`: an input reads its source. `"write"`: an output writes a parameter. |
| `source`, `target`             | The parameters.                                          |
| `source_owner`, `target_owner` | The nodes or operations it was found under (nil for an external parameter). |
| `source_reach`, `target_reach` | How the graph reaches each end: `"local"`, `"ancestor local"`, `"foreign local"`, `"graph pIn"`, `"graph pOut"`, `"pIn"`, `"pOut"`, `"foreign pIn"`, `"foreign pOut"`, `"target"`, `"operation pIn"`, `"operation"`, `"external"`. |
| `type`, `shared`               | The type of the parameter read or written; a shared input. |

### Uses

A use is something a building block does with a message, a data array, or a script. The node
holds its uses in `node.uses`, and the model holds all of them in `m.uses`.

| `kind`         | Building blocks                                             |
|----------------|-------------------------------------------------------------|
| `"send"`       | Send Message, Broadcast Message, Send Message To Group      |
| `"wait"`       | Wait Message, Switch On Message, Check For Message: the receivers |
| `"message"`    | Any other block with a Message input                        |
| `"read"`       | Get Cell, Get Row, Get Key Row, Iterator, Iterator If       |
| `"write"`      | Set Cell, Set Row, Add Row, Remove Row, Clear Array         |
| `"activate"`   | Activate Script, Execute Script                             |
| `"deactivate"` | Deactivate Script                                           |

| Field                 | Meaning                                                         |
|-----------------------|-----------------------------------------------------------------|
| `node`, `graph`, `root`, `model` | Where the use is.                                    |
| `key_param`           | The input naming what it acts on: the message, array, or script. |
| `key_value`           | How that input gets its value, as `origin_kind`.                |
| `message`             | Message uses: the message name, when saved.                     |
| `object`              | Array and script uses: the array (an object) or the script (its node), when known. |
| `object_name`         | The name a by-name lookup finds the array or script by.         |
| `column`, `column_name` | Cell and column blocks: the column index, and its name when the array names it. |
| `route`               | Sends and receivers: `"object"`, `"group"`, `"broadcast"`, or `"unknown"`. |
| `dest`                | Sends: the object (or group) it sends to, when the file holds it. |
| `listener`            | Receivers: the object whose messages it receives: its target, or the object its script belongs to. |
| `route_name`          | The name a by-name lookup finds the dest or listener by.        |
| `broadcast_class`     | Broadcast Message: the class it sends to (`"CKBeObject"`).      |

Scripts often find an array, a script, or a message's destination by name at run time ("Get
Object By Name" or "Convert" of a saved string). The use then has that name in `object_name`
or `route_name`, and the object too when the same file holds it. A key computed in any other
way stays unknown.

| Method                  | Returns                                                       |
|-------------------------|---------------------------------------------------------------|
| `u:key()`               | What it acts on: the message name, the object or script, the looked-up name, or nil. |
| `u:key_text()`          | That as text, quoting names.                                  |
| `u:is_message()`, `u:is_array()`, `u:is_script()` | Its family.                         |
| `u:reaches(receiver, set)` | For a send: whether it delivers its message to receiver `receiver`: true, false, or nil when that depends on what the scripts compute at run time. See [Several files](#several-files). |

### Objects

An object table stands for any other object the model refers to: an owner, a dest, an array, a
saved object value. It has `id`, `name`, `class` (`"CKDataArray"`), `class_id`, `classes` (a set
of its class and the classes it derives from: `obj.classes.CKBeObject`), `members` for a group
(object tables), and `missing` when the file has no such object. An object the model holds
itself (a script, say) is that table instead.

### Model methods

| Method               | Returns                                                          |
|----------------------|------------------------------------------------------------------|
| `m:get(id)`          | The item or object of that id.                                   |
| `m:walk()`           | An iterator over every node.                                     |
| `m:find(criteria)`   | The nodes matching the [criteria](#criteria), in document order. |
| `m:find_uses(criteria)` | The uses matching them.                                       |
| `m:messages()`       | Per message: `{ name, key, send = {...}, wait = {...}, message = {...} }`, sorted by name. |
| `m:arrays()`         | Per array: `{ name, key, read = {...}, write = {...} }`.         |
| `m:scripts()`        | Per script activated: `{ name, key, activate = {...}, deactivate = {...} }`. |
| `m:unresolved()`     | The uses whose key is computed at run time.                      |

The lists in a group are missing when empty (`group.write or {}`). `key` is the message name,
the array or script, or the name it is looked up by.

### Criteria

`find` and `find_uses` take a table of field criteria; an item matches when all of them do.
A criterion is a value the field must equal, a list of values (any of them), or a function the
field value must satisfy:

```lua
model:find{ proto = "Timer" }
model:find{ kind = { "Script", "Graph" } }
model:find{ kind = "Graph", name = function(n) return n:find("Ball") end }
model:find{ parent = model:get(877) }
model:get(3128):find{ proto = "Send Message" }      -- inside one script
model:find_uses{ kind = "send", message = "Ball Off" }
```

## Several files

The files of a game share objects by name: `base.cmo` sends messages to `All_Gameplay`, which
`Gameplay.nmo` holds, and looks up arrays such as `CurrentLevel` by name. `script.join(models)`
joins the models of several files:

| Method                  | Returns                                                       |
|-------------------------|---------------------------------------------------------------|
| `set:object(name, class)` | An object of that name (and class) in any of the files; for a group, one with members. |
| `set:find_uses(criteria)` | The uses of every file matching the criteria.               |
| `set:receivers(send)`   | The receivers a send reaches, and those it may reach: two lists of uses. |
| `set:senders(receiver)` | The sends reaching a receiver, and those that may.            |

`send:reaches(receiver, set)` decides delivery the way Virtools routes messages:

- **Send Message** reaches a receiver listening on its dest.
- **Send Message To Group** reaches the receivers of the group's members, and of the group
  itself (Ballance's scripts on a group wait for what is sent to it).
- **Broadcast Message** reaches every receiver whose listener derives from its class.
- A receiver listens on its target object, or on the object its script belongs to.

Objects of different files are the same object when they have the same name, and the same
class when both are known. When the dest or the listener is computed at run time, or a group's
members are in no loaded file, the answer is nil: "may reach".

```lua
local script = require("nmo.script")
local set = script.join(models)
for _, send in ipairs(set:find_uses{ kind = "send", message = "BallNav activate" }) do
    local sure, maybe = set:receivers(send)
    for _, receiver in ipairs(sure) do
        print(send.node.label, "->", receiver.root.label, receiver.model.name)
    end
end
```

```
$ nmo script analyze route.lua Gameplay.nmo Sound.nmo base.cmo
Send Message#584	->	Gameplay_Ingame#3128	Gameplay.nmo
Send Message#845	->	Sound_Manager#1229	Sound.nmo
...
```

## Adding methods

The classes are fields of the module (`Model`, `Node`, `Io`, `Param`, `Operation`, `Link`,
`Edge`, `Use`, `Object`, `Set`). Methods added to them apply to every model:

```lua
local script = require("nmo.script")

function script.Node:block_count()
    local count = 0
    for node in self:walk() do
        if node.kind == "BB" then count = count + 1 end
    end
    return count
end

print(model:get(3128):block_count())   -- 267
```

## Limits

- Message names, array names, and destinations are known when they are saved in the file or
  looked up by a saved name. Anything a script computes otherwise (a name built from a level
  number, an object picked at run time) is reported as unknown, with how its input gets its
  value.
- Only the building blocks listed under [Uses](#uses) have known semantics. Other blocks with a
  Message input are `"message"` uses with no route.
- `origin_kind` and message routes look at one file at a time; joining files is by name only.
