package store

import (
	"context"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"errors"
	_ "modernc.org/sqlite"
	"net/url"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type Store struct {
	DB            *sql.DB
	write         sync.Mutex
	AccountVaults int
	AccountBytes  int64
}

func UUID() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	b[6] = (b[6] & 15) | 64
	b[8] = (b[8] & 63) | 128
	h := hex.EncodeToString(b)
	return h[:8] + "-" + h[8:12] + "-" + h[12:16] + "-" + h[16:20] + "-" + h[20:]
}
func Open(path string) (*Store, error) {
	absolute, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	uriPath := filepath.ToSlash(absolute)
	if !strings.HasPrefix(uriPath, "/") {
		uriPath = "/" + uriPath
	}
	u := url.URL{Scheme: "file", Path: uriPath}
	q := u.Query()
	for _, p := range []string{"foreign_keys(1)", "journal_mode(WAL)", "synchronous(FULL)", "busy_timeout(5000)"} {
		q.Add("_pragma", p)
	}
	u.RawQuery = q.Encode()
	db, err := sql.Open("sqlite", u.String())
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(4)
	db.SetMaxIdleConns(4)
	s := &Store{DB: db, AccountVaults: 100, AccountBytes: 1 << 30}
	if err = s.migrate(); err != nil {
		db.Close()
		return nil, err
	}
	return s, nil
}
func (s *Store) Close() error { return s.DB.Close() }
func (s *Store) migrate() error {
	var version int
	if err := s.DB.QueryRow("PRAGMA user_version").Scan(&version); err != nil {
		return err
	}
	if version > 2 {
		return errors.New("UNSUPPORTED_SCHEMA_VERSION")
	}
	if version == 2 {
		return nil
	}
	tx, err := s.DB.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if version == 0 {
		_, err = tx.Exec(`
 CREATE TABLE accounts(id INTEGER PRIMARY KEY,username TEXT NOT NULL UNIQUE,password_hash BLOB NOT NULL,salt BLOB NOT NULL,
 argon_time INTEGER NOT NULL,argon_memory INTEGER NOT NULL,argon_threads INTEGER NOT NULL,credential_version INTEGER NOT NULL DEFAULT 1,created_at TEXT NOT NULL);
 CREATE TABLE sessions(token_hash BLOB PRIMARY KEY,account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 expires_at INTEGER NOT NULL,credential_version INTEGER NOT NULL);
 CREATE INDEX sessions_account ON sessions(account_id);
 CREATE TABLE vaults(account_id INTEGER NOT NULL REFERENCES accounts(id),uid TEXT NOT NULL,epoch TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>0),
 name TEXT NOT NULL,remark TEXT NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,PRIMARY KEY(account_id,uid));
 CREATE TABLE nodes(account_id INTEGER NOT NULL,vault_uid TEXT NOT NULL,sync_id TEXT NOT NULL,remote_id INTEGER NOT NULL,
 parent_sync_id TEXT,sequence INTEGER NOT NULL,title TEXT NOT NULL,node_type INTEGER NOT NULL,created_time INTEGER NOT NULL,modified_time INTEGER NOT NULL,
 format_type INTEGER NOT NULL,rtf BLOB NOT NULL,content_hash TEXT NOT NULL,PRIMARY KEY(account_id,vault_uid,sync_id),UNIQUE(account_id,vault_uid,remote_id),
 FOREIGN KEY(account_id,vault_uid) REFERENCES vaults(account_id,uid) ON DELETE CASCADE);
 CREATE TABLE operations(account_id INTEGER NOT NULL REFERENCES accounts(id),operation_id TEXT NOT NULL,request_hash TEXT NOT NULL,result TEXT NOT NULL,
 completed_at TEXT NOT NULL,PRIMARY KEY(account_id,operation_id));
 PRAGMA user_version=1;`)
		if err != nil {
			return err
		}
	}
	if _, err = tx.Exec(realtimeSchema); err != nil {
		return err
	}
	if _, err = tx.Exec(`INSERT OR IGNORE INTO sync_changes SELECT n.account_id,n.vault_uid,n.sync_id,v.revision,0 FROM nodes n JOIN vaults v ON v.account_id=n.account_id AND v.uid=n.vault_uid; PRAGMA user_version=2;`); err != nil {
		return err
	}
	return tx.Commit()
}
func timestamp() string { return time.Now().UTC().Format(time.RFC3339Nano) }
func (s *Store) transaction(ctx context.Context, fn func(*sql.Tx) error) error {
	s.write.Lock()
	defer s.write.Unlock()
	tx, err := s.DB.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if err = fn(tx); err != nil {
		return err
	}
	return tx.Commit()
}
