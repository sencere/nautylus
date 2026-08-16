<?php
declare(strict_types=1);

final class Nautylus
{
    private const CDEF = <<<'CDEF'
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
CDEF;

    private static ?FFI $ffi = null;

    public static function ffi(): FFI
    {
        if (self::$ffi === null) {
            $lib = getenv('NAUTYLUS_LIB');
            if (!$lib) {
                $lib = dirname(__DIR__, 2) . '/build/libnautylus.so';
            }
            self::$ffi = FFI::cdef(self::CDEF, $lib);
        }
        return self::$ffi;
    }
}

final class NautylusGraph
{
    private FFI $ffi;
    private ?FFI\CData $graph;

    private function __construct(FFI $ffi, FFI\CData $graph)
    {
        $this->ffi = $ffi;
        $this->graph = $graph;
    }

    public static function create(string $path): self
    {
        $ffi = Nautylus::ffi();
        $out = $ffi->new('ng_graph*[1]');
        self::check($ffi, $ffi->ng_create(FFI::addr($out[0]), $path));
        return new self($ffi, $out[0]);
    }

    public static function open(string $path): self
    {
        $ffi = Nautylus::ffi();
        $out = $ffi->new('ng_graph*[1]');
        self::check($ffi, $ffi->ng_open(FFI::addr($out[0]), $path));
        return new self($ffi, $out[0]);
    }

    public function close(): void
    {
        if ($this->graph !== null) {
            $this->ffi->ng_close($this->graph);
            $this->graph = null;
        }
    }

    public function __destruct()
    {
        $this->close();
    }

    public function save(): void
    {
        self::check($this->ffi, $this->ffi->ng_save($this->handle()));
    }

    public function symbol(string $name): int
    {
        $out = $this->ffi->new('ng_symbol_id[1]');
        self::check($this->ffi, $this->ffi->ng_symbol($this->handle(), $name, $out));
        return (int)$out[0];
    }

    /** @param int[] $labels */
    public function createNode(array $labels = []): int
    {
        $out = $this->ffi->new('ng_node_id[1]');
        if ($labels) {
            $labelArray = $this->ffi->new('ng_symbol_id[' . count($labels) . ']');
            foreach (array_values($labels) as $i => $label) {
                $labelArray[$i] = $label;
            }
            self::check($this->ffi, $this->ffi->ng_node_create(
                $this->handle(),
                $labelArray,
                count($labels),
                $out
            ));
        } else {
            self::check($this->ffi, $this->ffi->ng_node_create($this->handle(), null, 0, $out));
        }
        return (int)$out[0];
    }

    public function createRelationship(int $source, int $type, int $target): int
    {
        $out = $this->ffi->new('ng_relationship_id[1]');
        self::check(
            $this->ffi,
            $this->ffi->ng_relationship_create($this->handle(), $source, $type, $target, $out)
        );
        return (int)$out[0];
    }

    public function setNode(int $node, int $key, string|int|float|bool $value): void
    {
        if (is_bool($value)) {
            self::check($this->ffi, $this->ffi->ng_node_set_bool($this->handle(), $node, $key, $value ? 1 : 0));
        } elseif (is_int($value)) {
            self::check($this->ffi, $this->ffi->ng_node_set_int64($this->handle(), $node, $key, $value));
        } elseif (is_float($value)) {
            self::check($this->ffi, $this->ffi->ng_node_set_double($this->handle(), $node, $key, $value));
        } else {
            self::check($this->ffi, $this->ffi->ng_node_set_string($this->handle(), $node, $key, $value));
        }
    }

    public function setRelationship(int $relationship, int $key, string|int|float|bool $value): void
    {
        if (is_bool($value)) {
            self::check($this->ffi, $this->ffi->ng_relationship_set_bool($this->handle(), $relationship, $key, $value ? 1 : 0));
        } elseif (is_int($value)) {
            self::check($this->ffi, $this->ffi->ng_relationship_set_int64($this->handle(), $relationship, $key, $value));
        } elseif (is_float($value)) {
            self::check($this->ffi, $this->ffi->ng_relationship_set_double($this->handle(), $relationship, $key, $value));
        } else {
            self::check($this->ffi, $this->ffi->ng_relationship_set_string($this->handle(), $relationship, $key, $value));
        }
    }

    public function nodeCount(): int
    {
        return (int)$this->ffi->ng_node_count($this->handle());
    }

    public function relationshipCount(): int
    {
        return (int)$this->ffi->ng_relationship_count($this->handle());
    }

    public function query(string $query, bool $mutate = false): string
    {
        $path = tempnam(sys_get_temp_dir(), 'nautylus-query-');
        if ($path === false) {
            throw new RuntimeException('could not create temporary query output file');
        }
        try {
            if ($mutate) {
                $changed = $this->ffi->new('int[1]');
                self::check($this->ffi, $this->ffi->ng_query_execute_file(
                    $this->handle(),
                    $query,
                    $path,
                    $changed
                ));
            } else {
                self::check($this->ffi, $this->ffi->ng_query_print_file(
                    $this->handle(),
                    $query,
                    $path
                ));
            }
            $result = file_get_contents($path);
            return $result === false ? '' : $result;
        } finally {
            @unlink($path);
        }
    }

    private function handle(): FFI\CData
    {
        if ($this->graph === null) {
            throw new RuntimeException('graph is closed');
        }
        return $this->graph;
    }

    private static function check(FFI $ffi, int $status): void
    {
        if ($status !== 0) {
            throw new RuntimeException(FFI::string($ffi->ng_status_name($status)));
        }
    }
}
