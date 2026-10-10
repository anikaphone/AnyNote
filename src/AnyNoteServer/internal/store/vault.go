package store

import (
	"anynote/server/internal/protocol"
	"context"
	"database/sql"
	"encoding/base64"
	"errors"
	"strconv"
)

type Vault struct {
	UID       string `json:"uid"`
	Epoch     string `json:"epoch"`
	Revision  string `json:"revision"`
	Name      string `json:"name"`
	Remark    string `json:"remark"`
	CreatedAt string `json:"created_at"`
	UpdatedAt string `json:"updated_at"`
}
type VaultList struct {
	Vaults     []Vault `json:"vaults"`
	NextCursor *string `json:"next_cursor"`
}

func readVault(ctx context.Context, tx *sql.Tx, account int64, uid string) (Vault, error) {
	var v Vault
	var revision int64
	err := tx.QueryRowContext(ctx, "SELECT uid,epoch,revision,name,remark,created_at,updated_at FROM vaults WHERE account_id=? AND uid=?", account, uid).
		Scan(&v.UID, &v.Epoch, &revision, &v.Name, &v.Remark, &v.CreatedAt, &v.UpdatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return v, ErrNotFound
	}
	v.Revision = strconv.FormatInt(revision, 10)
	return v, err
}
func readNodes(ctx context.Context, tx *sql.Tx, account int64, v Vault) (protocol.Snapshot, error) {
	s := protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: v.UID, Name: v.Name, Nodes: []protocol.Node{}}
	rows, err := tx.QueryContext(ctx, `SELECT sync_id,remote_id,parent_sync_id,sequence,title,node_type,created_time,modified_time,format_type,rtf,content_hash
 FROM nodes WHERE account_id=? AND vault_uid=? ORDER BY sync_id`, account, v.UID)
	if err != nil {
		return s, err
	}
	defer rows.Close()
	for rows.Next() {
		var n protocol.Node
		var id, created, modified int64
		if err = rows.Scan(&n.SyncID, &id, &n.ParentSyncID, &n.Sequence, &n.Title, &n.NodeType, &created, &modified, &n.FormatType, &n.RTF, &n.ContentHash); err != nil {
			return s, err
		}
		n.SourceID = strconv.FormatInt(id, 10)
		n.CreatedTime = strconv.FormatInt(created, 10)
		n.ModifiedTime = strconv.FormatInt(modified, 10)
		n.RTFBase64 = base64.StdEncoding.EncodeToString(n.RTF)
		s.Nodes = append(s.Nodes, n)
	}
	return s, rows.Err()
}
func (s *Store) Read(ctx context.Context, p Principal, uid, epoch, revision string) (Vault, protocol.Snapshot, error) {
	var v Vault
	var snapshot protocol.Snapshot
	tx, err := s.DB.BeginTx(ctx, &sql.TxOptions{ReadOnly: true})
	if err != nil {
		return v, snapshot, err
	}
	defer tx.Rollback()
	if err = Authorize(ctx, tx, p); err != nil {
		return v, snapshot, err
	}
	v, err = readVault(ctx, tx, p.AccountID, uid)
	if err != nil {
		return v, snapshot, err
	}
	if (epoch != "" || revision != "") && (v.Epoch != epoch || v.Revision != revision) {
		return v, snapshot, errors.New("VERSION_CONFLICT")
	}
	snapshot, err = readNodes(ctx, tx, p.AccountID, v)
	if err != nil {
		return v, snapshot, err
	}
	return v, snapshot, tx.Commit()
}
func (s *Store) List(ctx context.Context, p Principal, cursor string, limit int) (VaultList, error) {
	result := VaultList{Vaults: []Vault{}}
	tx, err := s.DB.BeginTx(ctx, &sql.TxOptions{ReadOnly: true})
	if err != nil {
		return result, err
	}
	defer tx.Rollback()
	if err = Authorize(ctx, tx, p); err != nil {
		return result, err
	}
	rows, err := tx.QueryContext(ctx, "SELECT uid,epoch,revision,name,remark,created_at,updated_at FROM vaults WHERE account_id=? AND uid>? ORDER BY uid LIMIT ?", p.AccountID, cursor, limit+1)
	if err != nil {
		return result, err
	}
	for rows.Next() {
		var v Vault
		var revision int64
		if err = rows.Scan(&v.UID, &v.Epoch, &revision, &v.Name, &v.Remark, &v.CreatedAt, &v.UpdatedAt); err != nil {
			rows.Close()
			return result, err
		}
		v.Revision = strconv.FormatInt(revision, 10)
		result.Vaults = append(result.Vaults, v)
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return result, err
	}
	if len(result.Vaults) > limit {
		result.Vaults = result.Vaults[:limit]
		cursor = result.Vaults[limit-1].UID
		result.NextCursor = &cursor
	}
	return result, tx.Commit()
}
