local ok, ffi = pcall(require, "ffi")
if not ok then
    error("Nautylus Lua binding requires LuaJIT FFI. Run it with `luajit`, not plain Lua.")
end

ffi.cdef[[
typedef unsigned long long ng_node_id;
typedef unsigned long long ng_relationship_id;
typedef unsigned long long ng_symbol_id;
typedef struct ng_graph ng_graph;
int ng_create(ng_graph** out, const char* path);
int ng_open(ng_graph** out, const char* path);
void ng_close(ng_graph* g);
int ng_save(ng_graph* g);
int ng_symbol(ng_graph* g, const char* text, ng_symbol_id* out);
int ng_node_create(ng_graph* g, const ng_symbol_id* labels, size_t n, ng_node_id* out);
int ng_relationship_create(
    ng_graph* g,
    ng_node_id source,
    ng_symbol_id type,
    ng_node_id target,
    ng_relationship_id* out);
int ng_node_set_string(ng_graph* g, ng_node_id node, ng_symbol_id key, const char* value);
int ng_node_set_int64(ng_graph* g, ng_node_id node, ng_symbol_id key, long long value);
int ng_node_set_double(ng_graph* g, ng_node_id node, ng_symbol_id key, double value);
int ng_node_set_bool(ng_graph* g, ng_node_id node, ng_symbol_id key, int value);
int ng_relationship_set_string(
    ng_graph* g,
    ng_relationship_id rel,
    ng_symbol_id key,
    const char* value);
int ng_relationship_set_int64(
    ng_graph* g,
    ng_relationship_id rel,
    ng_symbol_id key,
    long long value);
int ng_relationship_set_double(
    ng_graph* g,
    ng_relationship_id rel,
    ng_symbol_id key,
    double value);
int ng_relationship_set_bool(
    ng_graph* g,
    ng_relationship_id rel,
    ng_symbol_id key,
    int value);
size_t ng_node_count(const ng_graph* g);
size_t ng_relationship_count(const ng_graph* g);
int ng_query_print_file(const ng_graph* g, const char* query, const char* output_path);
int ng_query_execute_file(ng_graph* g, const char* query, const char* output_path, int* mutated);
const char* ng_status_name(int s);
]]

local function dirname(path)
    return path:match("^(.*)/[^/]*$") or "."
end

local function file_exists(path)
    local file = io.open(path, "rb")
    if file then
        file:close()
        return true
    end
    return false
end

local function default_library_path()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    return dirname(dirname(dirname(source))) .. "/build/libnautylus.so"
end

local function load_library()
    local path = os.getenv("NAUTYLUS_LIB") or default_library_path()
    if not file_exists(path) then
        error(
            "Nautylus shared library not found at "
                .. path
                .. ". Run `make bindings` from the repository root or set NAUTYLUS_LIB."
        )
    end
    local loaded, lib = pcall(ffi.load, path)
    if not loaded then
        error(
            "Could not load Nautylus shared library at "
                .. path
                .. ": "
                .. tostring(lib)
                .. ". Rebuild with `make bindings` or set NAUTYLUS_LIB."
        )
    end
    return lib
end

local C = load_library()
local M = {}
local Graph = {}
Graph.__index = Graph

local function check(status)
    if status ~= 0 then
        error(ffi.string(C.ng_status_name(status)))
    end
end

local function as_number(value)
    return tonumber(value)
end

local function temp_path()
    local path = os.tmpname()
    if not path then
        error("could not create temporary query output path")
    end
    return path
end

local function read_file(path)
    local file = assert(io.open(path, "rb"))
    local text = file:read("*a")
    file:close()
    return text
end

local function wrap(handle)
    return setmetatable({ handle = ffi.gc(handle, C.ng_close) }, Graph)
end

function M.create(path)
    local out = ffi.new("ng_graph*[1]")
    check(C.ng_create(out, path))
    return wrap(out[0])
end

function M.open(path)
    local out = ffi.new("ng_graph*[1]")
    check(C.ng_open(out, path))
    return wrap(out[0])
end

function Graph:close()
    if self.handle ~= nil then
        ffi.gc(self.handle, nil)
        C.ng_close(self.handle)
        self.handle = nil
    end
end

function Graph:save()
    check(C.ng_save(self:require_handle()))
end

function Graph:require_handle()
    if self.handle == nil then
        error("graph is closed")
    end
    return self.handle
end

function Graph:symbol(name)
    local out = ffi.new("ng_symbol_id[1]")
    check(C.ng_symbol(self:require_handle(), name, out))
    return as_number(out[0])
end

function Graph:create_node(labels)
    labels = labels or {}
    local out = ffi.new("ng_node_id[1]")
    if #labels > 0 then
        local label_array = ffi.new("ng_symbol_id[?]", #labels)
        for i, label in ipairs(labels) do
            label_array[i - 1] = label
        end
        check(C.ng_node_create(self:require_handle(), label_array, #labels, out))
    else
        check(C.ng_node_create(self:require_handle(), nil, 0, out))
    end
    return as_number(out[0])
end

function Graph:create_relationship(source, relationship_type, target)
    local out = ffi.new("ng_relationship_id[1]")
    check(C.ng_relationship_create(self:require_handle(), source, relationship_type, target, out))
    return as_number(out[0])
end

function Graph:set_node(node, key, value)
    local handle = self:require_handle()
    local kind = type(value)
    if kind == "boolean" then
        check(C.ng_node_set_bool(handle, node, key, value and 1 or 0))
    elseif kind == "number" and value == math.floor(value) then
        check(C.ng_node_set_int64(handle, node, key, value))
    elseif kind == "number" then
        check(C.ng_node_set_double(handle, node, key, value))
    else
        check(C.ng_node_set_string(handle, node, key, tostring(value)))
    end
end

function Graph:set_relationship(relationship, key, value)
    local handle = self:require_handle()
    local kind = type(value)
    if kind == "boolean" then
        check(C.ng_relationship_set_bool(handle, relationship, key, value and 1 or 0))
    elseif kind == "number" and value == math.floor(value) then
        check(C.ng_relationship_set_int64(handle, relationship, key, value))
    elseif kind == "number" then
        check(C.ng_relationship_set_double(handle, relationship, key, value))
    else
        check(C.ng_relationship_set_string(handle, relationship, key, tostring(value)))
    end
end

function Graph:node_count()
    return as_number(C.ng_node_count(self:require_handle()))
end

function Graph:relationship_count()
    return as_number(C.ng_relationship_count(self:require_handle()))
end

function Graph:query(query, mutate)
    local path = temp_path()
    local ok_query, err = pcall(function()
        if mutate then
            local changed = ffi.new("int[1]")
            check(C.ng_query_execute_file(self:require_handle(), query, path, changed))
        else
            check(C.ng_query_print_file(self:require_handle(), query, path))
        end
        return read_file(path)
    end)
    os.remove(path)
    if not ok_query then
        error(err)
    end
    return err
end

M.Graph = Graph
return M
