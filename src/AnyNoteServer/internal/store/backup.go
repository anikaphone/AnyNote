package store

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"os"
	"path/filepath"
)

func (s *Store) Integrity(ctx context.Context) error {
	var result string
	if err := s.DB.QueryRowContext(ctx, "PRAGMA integrity_check").Scan(&result); err != nil {
		return err
	}
	if result != "ok" {
		return errors.New("BACKUP_CORRUPT")
	}
	rows, err := s.DB.QueryContext(ctx, "PRAGMA foreign_key_check")
	if err != nil {
		return err
	}
	defer rows.Close()
	if rows.Next() {
		return errors.New("BACKUP_FOREIGN_KEY_FAILURE")
	}
	return rows.Err()
}

// Backup uses SQLite's consistent VACUUM snapshot, including committed WAL data.
// The destination must not exist; no unrelated backup is ever overwritten.
func (s *Store) Backup(ctx context.Context, destination string) error {
	absolute, err := filepath.Abs(destination)
	if err != nil {
		return err
	}
	if _, err = os.Stat(absolute); !errors.Is(err, os.ErrNotExist) {
		return errors.New("BACKUP_DESTINATION_EXISTS")
	}
	s.write.Lock()
	defer s.write.Unlock()
	if _, err = s.DB.ExecContext(ctx, "VACUUM INTO ?", absolute); err != nil {
		return err
	}
	if err = os.Chmod(absolute, 0600); err != nil {
		return err
	}
	file, err := os.OpenFile(absolute, os.O_RDWR, 0600)
	if err != nil {
		return err
	}
	err = file.Sync()
	closeErr := file.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	verify, err := Open(absolute)
	if err != nil {
		return err
	}
	defer verify.Close()
	return verify.Integrity(ctx)
}

// RestoreInto always writes a new database path. Stop the service before switching
// its data_dir to this restored directory. The original service data is retained.
func RestoreInto(ctx context.Context, backup, destination string) (result error) {
	// Reject existing destinations (including dangling symlinks and SQLite sidecars).
	// Publication below also checks existence atomically, so a concurrent creator
	// cannot be overwritten after this early check.
	for _, suffix := range []string{"", "-wal", "-shm", "-journal"} {
		if _, err := os.Lstat(destination + suffix); err == nil {
			return fmt.Errorf("restore destination exists: %w", os.ErrExist)
		} else if !errors.Is(err, os.ErrNotExist) {
			return err
		}
	}
	source, err := os.Open(backup)
	if err != nil {
		return err
	}
	defer source.Close()
	staging, err := os.MkdirTemp(filepath.Dir(destination), ".anynote-restore-")
	if err != nil {
		return err
	}
	defer func() {
		if err := os.RemoveAll(staging); err != nil {
			if result != nil {
				result = errors.Join(result, fmt.Errorf("restore staging cleanup: %w", err))
			} else {
				// The destination is already complete; do not report a failed restore
				// and invite a retry of a successfully published database.
				slog.Warn("restore completed but staging cleanup failed", "error", err)
			}
		}
	}()
	staged := filepath.Join(staging, "restore.sqlite")
	output, err := os.OpenFile(staged, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	_, copyErr := io.Copy(output, source)
	syncErr := output.Sync()
	closeErr := output.Close()
	if copyErr != nil {
		return copyErr
	}
	if syncErr != nil {
		return syncErr
	}
	if closeErr != nil {
		return closeErr
	}
	restored, err := Open(staged)
	if err != nil {
		return err
	}
	defer func() {
		if restored != nil {
			result = errors.Join(result, restored.Close())
		}
	}()
	if err = restored.Integrity(ctx); err != nil {
		return err
	}
	err = restored.transaction(ctx, func(tx *sql.Tx) error {
		if _, err := tx.ExecContext(ctx, "DELETE FROM sessions; DELETE FROM operations;"); err != nil {
			return err
		}
		rows, err := tx.QueryContext(ctx, "SELECT account_id,uid FROM vaults")
		if err != nil {
			return err
		}
		type key struct {
			account int64
			uid     string
		}
		keys := []key{}
		for rows.Next() {
			var k key
			if err = rows.Scan(&k.account, &k.uid); err != nil {
				rows.Close()
				return err
			}
			keys = append(keys, k)
		}
		err = rows.Err()
		rows.Close()
		if err != nil {
			return err
		}
		for _, k := range keys {
			if _, err = tx.ExecContext(ctx, "UPDATE vaults SET epoch=?,updated_at=? WHERE account_id=? AND uid=?", UUID(), timestamp(), k.account, k.uid); err != nil {
				return err
			}
		}
		return nil
	})
	if err != nil {
		return err
	}
	var busy, log, done int
	if err = restored.DB.QueryRowContext(ctx, "PRAGMA wal_checkpoint(TRUNCATE)").Scan(&busy, &log, &done); err != nil {
		return err
	}
	if busy != 0 || log != done {
		return errors.New("RESTORE_CHECKPOINT_BUSY")
	}
	if err = restored.Close(); err != nil {
		return err
	}
	restored = nil
	if err = ctx.Err(); err != nil {
		return err
	}
	// Link publishes the fully checkpointed, closed file without replacement on
	// both Windows and Unix. Rename would overwrite an existing file on Unix.
	// Staging is on the same filesystem; unsupported hard links fail safely.
	return os.Link(staged, destination)
}
