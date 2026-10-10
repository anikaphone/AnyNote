package store

import (
	"anynote/server/internal/protocol"
	"context"
	"database/sql"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBackupRestoreEpochAndSessions(t *testing.T) {
	dir := t.TempDir()
	ctx := context.Background()
	s, err := Open(filepath.Join(dir, "live.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if err = s.Register(ctx, "restore_user", "test-password"); err != nil {
		t.Fatal(err)
	}
	login, err := s.Login(ctx, "restore_user", "test-password")
	if err != nil {
		t.Fatal(err)
	}
	p, err := s.Authenticate(ctx, login.Token)
	if err != nil {
		t.Fatal(err)
	}
	snapshot := protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: "0123456789abcdef", Name: "restore", Nodes: []protocol.Node{}}
	operation, err := s.Write(ctx, p, snapshot.UID, "", "snapshot", protocol.WriteRequest{Precondition: protocol.Precondition{OperationID: UUID(), CreateOnly: true}, Snapshot: snapshot}, protocol.DefaultLimits)
	if err != nil {
		t.Fatal(err)
	}
	backup := filepath.Join(dir, "backup.sqlite")
	if err = s.Backup(ctx, backup); err != nil {
		t.Fatal(err)
	}
	if err = s.Backup(ctx, backup); err == nil {
		t.Fatal("overwrote backup")
	}
	restoredPath := filepath.Join(dir, "restored.sqlite")
	if err = RestoreInto(ctx, backup, restoredPath); err != nil {
		t.Fatal(err)
	}
	restored, err := Open(restoredPath)
	if err != nil {
		t.Fatal(err)
	}
	defer restored.Close()
	if _, err = restored.Authenticate(ctx, login.Token); err != ErrUnauthorized {
		t.Fatal("old session survived", err)
	}
	fresh, err := restored.Login(ctx, "restore_user", "test-password")
	if err != nil {
		t.Fatal(err)
	}
	principal, err := restored.Authenticate(ctx, fresh.Token)
	if err != nil {
		t.Fatal(err)
	}
	vault, _, err := restored.Read(ctx, principal, snapshot.UID, "", "")
	if err != nil {
		t.Fatal(err)
	}
	if vault.Epoch == operation.Epoch {
		t.Fatal("epoch survived restore")
	}
	_, err = restored.Write(ctx, principal, snapshot.UID, "", "snapshot", protocol.WriteRequest{Precondition: protocol.Precondition{OperationID: UUID(), Epoch: operation.Epoch, Revision: operation.Revision}, Snapshot: snapshot}, protocol.DefaultLimits)
	if err == nil || err.Error() != "VERSION_CONFLICT" {
		t.Fatal("stale write survived restore", err)
	}
	if _, err = restored.Operation(ctx, principal, operation.OperationID); err != ErrNotFound {
		t.Fatal("stale operation survived restore", err)
	}
}

func TestRestoreFailureAllowsRetry(t *testing.T) {
	ctx := context.Background()
	dir := t.TempDir()
	live, err := Open(filepath.Join(dir, "live.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	defer live.Close()
	if err = live.Register(ctx, "retry_user", "test-password"); err != nil {
		t.Fatal(err)
	}
	login, err := live.Login(ctx, "retry_user", "test-password")
	if err != nil {
		t.Fatal(err)
	}
	principal, err := live.Authenticate(ctx, login.Token)
	if err != nil {
		t.Fatal(err)
	}
	snapshot := protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: "0123456789abcdef", Name: "retry", Nodes: []protocol.Node{}}
	if _, err = live.Write(ctx, principal, snapshot.UID, "", "snapshot", protocol.WriteRequest{Precondition: protocol.Precondition{OperationID: UUID(), CreateOnly: true}, Snapshot: snapshot}, protocol.DefaultLimits); err != nil {
		t.Fatal(err)
	}
	good := filepath.Join(dir, "good.sqlite")
	if err = live.Backup(ctx, good); err != nil {
		t.Fatal(err)
	}
	bytes, err := os.ReadFile(good)
	if err != nil {
		t.Fatal(err)
	}
	for _, test := range []struct{ name, sql, want string }{
		{"open", "", "not a database"},
		{"future_schema", "PRAGMA user_version=99", "UNSUPPORTED_SCHEMA_VERSION"},
		{"migration", "PRAGMA user_version=0", "already exists"},
		{"integrity", "PRAGMA foreign_keys=OFF; UPDATE sessions SET account_id=99999", "BACKUP_FOREIGN_KEY_FAILURE"},
		{"session_reset", "CREATE TRIGGER reject_restore BEFORE DELETE ON sessions BEGIN SELECT RAISE(ABORT,'restore_session_failure'); END", "restore_session_failure"},
		{"epoch_reset", "CREATE TRIGGER reject_restore BEFORE UPDATE OF epoch ON vaults BEGIN SELECT RAISE(ABORT,'restore_epoch_failure'); END", "restore_epoch_failure"},
	} {
		t.Run(test.name, func(t *testing.T) {
			root := t.TempDir()
			bad := filepath.Join(root, "bad.sqlite")
			data := bytes
			if test.sql == "" {
				data = []byte("not a SQLite backup")
			}
			if err := os.WriteFile(bad, data, 0600); err != nil {
				t.Fatal(err)
			}
			if test.sql != "" {
				db, err := sql.Open("sqlite", bad)
				if err != nil {
					t.Fatal(err)
				}
				db.SetMaxOpenConns(1)
				_, execErr := db.Exec(test.sql)
				closeErr := db.Close()
				if err = errors.Join(execErr, closeErr); err != nil {
					t.Fatal(err)
				}
			}
			before, err := os.ReadFile(bad)
			if err != nil {
				t.Fatal(err)
			}
			// Keep fixture-editing sidecars separate from restore output. On Windows
			// the SQLite driver can leave a transient renamed SHM beside the source.
			outputDir := filepath.Join(root, "destination")
			if err = os.Mkdir(outputDir, 0700); err != nil {
				t.Fatal(err)
			}
			destination := filepath.Join(outputDir, "restored.sqlite")
			if err = RestoreInto(ctx, bad, destination); err == nil || !strings.Contains(err.Error(), test.want) {
				t.Fatalf("wrong failure: %v", err)
			}
			entries, err := os.ReadDir(outputDir)
			if err != nil {
				t.Fatal(err)
			}
			if len(entries) != 0 {
				t.Fatalf("incomplete restore files remain: %v", entries)
			}
			after, err := os.ReadFile(bad)
			if err != nil || string(after) != string(before) {
				t.Fatalf("source backup changed: %v", err)
			}
			if err = RestoreInto(ctx, good, destination); err != nil {
				t.Fatalf("retry failed: %v", err)
			}
			completed, err := os.ReadFile(destination)
			if err != nil {
				t.Fatal(err)
			}
			if err = RestoreInto(ctx, good, destination); err == nil {
				t.Fatal("overwrote completed restore")
			}
			after, err = os.ReadFile(destination)
			if err != nil || string(after) != string(completed) {
				t.Fatalf("existing destination changed: %v", err)
			}
		})
	}
}
