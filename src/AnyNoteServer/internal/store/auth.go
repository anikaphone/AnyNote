package store

import (
	"anynote/server/internal/protocol"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"encoding/base64"
	"errors"
	"golang.org/x/crypto/argon2"
	"strconv"
	"time"
)

var ErrUnauthorized = errors.New("UNAUTHORIZED")
var ErrConflict = errors.New("CONFLICT")
var ErrNotFound = errors.New("NOT_FOUND")
var ErrQuota = errors.New("QUOTA_EXCEEDED")

type Principal struct {
	AccountID         int64
	Username          string
	CredentialVersion int64
	TokenHash         []byte
}
type LoginResult struct {
	AccountID string `json:"account_id"`
	Username  string `json:"username"`
	Token     string `json:"token"`
	ExpiresAt string `json:"expires_at"`
}

func secret(password string) (salt, hash []byte) {
	salt = make([]byte, 16)
	if _, err := rand.Read(salt); err != nil {
		panic(err)
	}
	hash = argon2.IDKey([]byte(password), salt, 3, 64*1024, 2, 32)
	return
}
func (s *Store) Register(ctx context.Context, username, password string) error {
	name, err := protocol.NormalizeAccount(username)
	if err != nil || !protocol.ValidPassword(password) {
		return errors.New("INVALID_CREDENTIALS")
	}
	salt, hash := secret(password)
	return s.transaction(ctx, func(tx *sql.Tx) error {
		var count int
		if err := tx.QueryRowContext(ctx, "SELECT count(*) FROM accounts WHERE username=?", name).Scan(&count); err != nil {
			return err
		}
		if count != 0 {
			return ErrConflict
		}
		_, err := tx.ExecContext(ctx, "INSERT INTO accounts(username,password_hash,salt,argon_time,argon_memory,argon_threads,created_at) VALUES(?,?,?,3,65536,2,?)", name, hash, salt, timestamp())
		return err
	})
}
func (s *Store) checkPassword(ctx context.Context, username, password string) (Principal, error) {
	var p Principal
	var salt, hash []byte
	var iterations, memory uint32
	var threads uint8
	err := s.DB.QueryRowContext(ctx, "SELECT id,username,password_hash,salt,argon_time,argon_memory,argon_threads,credential_version FROM accounts WHERE username=?", username).
		Scan(&p.AccountID, &p.Username, &hash, &salt, &iterations, &memory, &threads, &p.CredentialVersion)
	if err != nil {
		if !errors.Is(err, sql.ErrNoRows) {
			return p, err
		}
		// Comparable work for nonexistent accounts prevents a cheap enumeration path.
		argon2.IDKey([]byte(password), make([]byte, 16), 3, 64*1024, 2, 32)
		return p, ErrUnauthorized
	}
	if iterations < 1 || iterations > 10 || memory < 8192 || memory > 262144 || threads < 1 || threads > 8 || len(hash) != 32 || len(salt) != 16 {
		return p, errors.New("INVALID_AUTH_STORE")
	}
	computed := argon2.IDKey([]byte(password), salt, iterations, memory, threads, 32)
	if subtle.ConstantTimeCompare(hash, computed) != 1 {
		return p, ErrUnauthorized
	}
	return p, nil
}
func (s *Store) Login(ctx context.Context, username, password string) (LoginResult, error) {
	var result LoginResult
	name, err := protocol.NormalizeAccount(username)
	if err != nil || !protocol.ValidPassword(password) {
		return result, ErrUnauthorized
	}
	p, err := s.checkPassword(ctx, name, password)
	if err != nil {
		return result, err
	}
	token := make([]byte, 32)
	if _, err = rand.Read(token); err != nil {
		return result, err
	}
	encoded := base64.RawURLEncoding.EncodeToString(token)
	digest := sha256.Sum256([]byte(encoded))
	expiry := time.Now().Add(7 * 24 * time.Hour)
	err = s.transaction(ctx, func(tx *sql.Tx) error {
		var version int64
		if err := tx.QueryRowContext(ctx, "SELECT credential_version FROM accounts WHERE id=?", p.AccountID).Scan(&version); err != nil {
			return err
		}
		if version != p.CredentialVersion {
			return ErrUnauthorized
		}
		if _, err := tx.ExecContext(ctx, "DELETE FROM sessions WHERE expires_at<=?", time.Now().Unix()); err != nil {
			return err
		}
		_, err := tx.ExecContext(ctx, "INSERT INTO sessions VALUES(?,?,?,?)", digest[:], p.AccountID, expiry.Unix(), version)
		return err
	})
	if err != nil {
		return result, err
	}
	return LoginResult{strconv.FormatInt(p.AccountID, 10), p.Username, encoded, expiry.UTC().Format(time.RFC3339)}, nil
}
func (s *Store) Authenticate(ctx context.Context, token string) (Principal, error) {
	var p Principal
	if len(token) != 43 {
		return p, ErrUnauthorized
	}
	hash := sha256.Sum256([]byte(token))
	p.TokenHash = hash[:]
	err := s.DB.QueryRowContext(ctx, `SELECT a.id,a.username,a.credential_version FROM sessions s JOIN accounts a ON a.id=s.account_id
 WHERE s.token_hash=? AND s.expires_at>? AND s.credential_version=a.credential_version`, hash[:], time.Now().Unix()).Scan(&p.AccountID, &p.Username, &p.CredentialVersion)
	if errors.Is(err, sql.ErrNoRows) {
		return p, ErrUnauthorized
	}
	return p, err
}
func Authorize(ctx context.Context, tx *sql.Tx, p Principal) error {
	var count int
	if err := tx.QueryRowContext(ctx, `SELECT count(*) FROM sessions s JOIN accounts a ON a.id=s.account_id WHERE s.token_hash=? AND s.account_id=?
 AND s.expires_at>? AND s.credential_version=a.credential_version`, p.TokenHash, p.AccountID, time.Now().Unix()).Scan(&count); err != nil {
		return err
	}
	if count != 1 {
		return ErrUnauthorized
	}
	return nil
}
func (s *Store) Logout(ctx context.Context, p Principal) error {
	return s.transaction(ctx, func(tx *sql.Tx) error {
		_, err := tx.ExecContext(ctx, "DELETE FROM sessions WHERE token_hash=? AND account_id=?", p.TokenHash, p.AccountID)
		return err
	})
}
func (s *Store) ChangePassword(ctx context.Context, p Principal, oldPassword, newPassword string) error {
	if !protocol.ValidPassword(oldPassword) || !protocol.ValidPassword(newPassword) {
		return errors.New("INVALID_PASSWORD")
	}
	verified, err := s.checkPassword(ctx, p.Username, oldPassword)
	if err != nil {
		return err
	}
	salt, hash := secret(newPassword)
	return s.transaction(ctx, func(tx *sql.Tx) error {
		if err := Authorize(ctx, tx, p); err != nil {
			return err
		}
		result, err := tx.ExecContext(ctx, `UPDATE accounts SET salt=?,password_hash=?,argon_time=3,argon_memory=65536,argon_threads=2,
  credential_version=credential_version+1 WHERE id=? AND credential_version=?`, salt, hash, p.AccountID, verified.CredentialVersion)
		if err != nil {
			return err
		}
		rows, err := result.RowsAffected()
		if err != nil {
			return err
		}
		if rows != 1 {
			return ErrUnauthorized
		}
		_, err = tx.ExecContext(ctx, "DELETE FROM sessions WHERE account_id=?", p.AccountID)
		return err
	})
}
