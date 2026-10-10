package protocol

import (
	"encoding/json"
	"os"
	"testing"
)

func TestRequestIdentity(t *testing.T) {
	f, err := os.Open("../../../../tests/fixtures/request-identity/v1.json")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	d := json.NewDecoder(f)
	d.UseNumber()
	var cases []struct {
		Descriptor map[string]any `json:"descriptor"`
		Hash       string         `json:"sha256"`
	}
	if err := d.Decode(&cases); err != nil {
		t.Fatal(err)
	}
	for _, test := range cases {
		h, err := RequestHash(test.Descriptor)
		if err != nil || h != test.Hash {
			t.Fatalf("identity %s %s %v", h, test.Hash, err)
		}
		test.Descriptor["path"] = "/different"
		h, err = RequestHash(test.Descriptor)
		if err != nil || h == test.Hash {
			t.Fatal("different request", err)
		}
	}
	for _, value := range []any{map[string]any{"operation_id": "forbidden"}, 1.5} {
		if _, err := RequestHash(value); err == nil {
			t.Fatal("accepted invalid descriptor")
		}
	}
}
