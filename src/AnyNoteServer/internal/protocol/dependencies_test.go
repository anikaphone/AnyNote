package protocol

import (
	"bytes"
	"database/sql"
	"path/filepath"
	"testing"

	"golang.org/x/crypto/argon2"
	_ "modernc.org/sqlite"
)

func TestPureGoDependencies(t *testing.T) {
	db, err := sql.Open("sqlite", filepath.Join(t.TempDir(), "probe.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	db.SetMaxOpenConns(1)
	if _, err = db.Exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL; CREATE TABLE probe (id INTEGER PRIMARY KEY, bytes BLOB)"); err != nil {
		t.Fatal(err)
	}
	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	if _, err = tx.Exec("INSERT INTO probe VALUES (1, ?)", []byte{0, 1, 255}); err != nil {
		t.Fatal(err)
	}
	if err = tx.Rollback(); err != nil {
		t.Fatal(err)
	}
	var count int
	if err = db.QueryRow("SELECT count(*) FROM probe").Scan(&count); err != nil || count != 0 {
		t.Fatalf("rollback: %d %v", count, err)
	}
	a := argon2.IDKey([]byte("test password"), []byte("0123456789abcdef"), 3, 64*1024, 1, 32)
	b := argon2.IDKey([]byte("wrong password"), []byte("0123456789abcdef"), 3, 64*1024, 1, 32)
	if len(a) != 32 || bytes.Equal(a, b) {
		t.Fatal("argon2 probe failed")
	}
}
