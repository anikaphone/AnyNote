package protocol

import (
	"encoding/json"
	"os"
	"strings"
	"testing"
)

func TestContractReferences(t *testing.T) {
	var schema, api map[string]any
	for path, target := range map[string]*map[string]any{
		"../../../../protocol/sync-v1.schema.json": &schema,
		"../../../../protocol/openapi.json":        &api,
	} {
		b, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		if err = json.Unmarshal(b, target); err != nil {
			t.Fatal(err)
		}
	}
	var walk func(any)
	walk = func(value any) {
		switch v := value.(type) {
		case map[string]any:
			if ref, ok := v["$ref"].(string); ok {
				ref = strings.TrimPrefix(ref, "./sync-v1.schema.json")
				if !strings.HasPrefix(ref, "#/$defs/") {
					t.Fatalf("unexpected reference %s", ref)
				}
				if _, ok := schema["$defs"].(map[string]any)[strings.TrimPrefix(ref, "#/$defs/")]; !ok {
					t.Fatalf("unresolved %s", ref)
				}
			}
			for _, item := range v {
				walk(item)
			}
		case []any:
			for _, item := range v {
				walk(item)
			}
		}
	}
	walk(schema)
	walk(api)
	seen := map[string]bool{}
	for _, path := range api["paths"].(map[string]any) {
		for _, raw := range path.(map[string]any) {
			op := raw.(map[string]any)
			id := op["operationId"].(string)
			if seen[id] {
				t.Fatalf("duplicate operation %s", id)
			}
			seen[id] = true
			if op["responses"] == nil {
				t.Fatal("missing responses", id)
			}
		}
	}
	if len(seen) != 15 {
		t.Fatalf("expected 15 API operations, got %d", len(seen))
	}
}
