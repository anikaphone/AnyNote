package api

import (
	"anynote/server/internal/protocol"
	"anynote/server/internal/store"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"mime"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

type bucket struct {
	count int
	until time.Time
}
type Server struct {
	Store               *store.Store
	RegistrationEnabled bool
	Limits              protocol.Limits
	Log                 *slog.Logger
	slots               chan struct{}
	authSlots           chan struct{}
	rateMu              sync.Mutex
	rates               map[string]bucket
}

func New(s *store.Store, registration bool) *Server {
	return &Server{Store: s, RegistrationEnabled: registration, Limits: protocol.DefaultLimits, Log: slog.Default(), slots: make(chan struct{}, 2), authSlots: make(chan struct{}, 2), rates: map[string]bucket{}}
}
func (s *Server) allow(key string) bool {
	s.rateMu.Lock()
	defer s.rateMu.Unlock()
	now := time.Now()
	for k, v := range s.rates {
		if now.After(v.until) {
			delete(s.rates, k)
		}
	}
	b, exists := s.rates[key]
	if !exists {
		if len(s.rates) >= 10000 {
			return false
		}
		b = bucket{until: now.Add(time.Minute)}
	}
	b.count++
	s.rates[key] = b
	return b.count <= 10
}
func errorStatus(err error) (int, string, bool) {
	code := err.Error()
	switch code {
	case "UNAUTHORIZED":
		return 401, code, false
	case "NOT_FOUND":
		return 404, code, false
	case "CONFLICT", "VERSION_CONFLICT", "UID_MISMATCH", "OPERATION_ID_REUSED", "OUTSIDE_SUBTREE", "REVISION_EXHAUSTED", "ID_EXHAUSTED":
		return 409, code, false
	case "CURSOR_EXPIRED":
		return 409, code, false
	case "LIMIT_EXCEEDED", "QUOTA_EXCEEDED":
		return 413, code, false
	case "HASH_MISMATCH":
		return 422, code, false
	case "RATE_LIMITED":
		return 429, code, true
	case "REGISTRATION_DISABLED":
		return 403, code, false
	case "BUSY":
		return 503, code, true
	case "INVALID_REQUEST", "INVALID_JSON", "DUPLICATE_FIELD", "INVALID_NODE", "INVALID_PRECONDITION", "INVALID_CREDENTIALS", "INVALID_PASSWORD", "INVALID_UUID", "INVALID_INTEGER", "INVALID_BASE64", "INVALID_HASH", "INVALID_UTF8", "INVALID_SNAPSHOT", "DUPLICATE_SYNC_ID", "DUPLICATE_SOURCE_ID", "MISSING_PARENT", "TREE_CYCLE", "UNSUPPORTED_VERSION", "UNSUPPORTED_FORMAT":
		return 400, code, false
	}
	var syntax *json.SyntaxError
	var typ *json.UnmarshalTypeError
	var large *http.MaxBytesError
	if errors.As(err, &large) {
		return 413, "LIMIT_EXCEEDED", false
	}
	if errors.As(err, &syntax) || errors.As(err, &typ) || errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) {
		return 400, "INVALID_JSON", false
	}
	return 503, "TEMPORARY_UNAVAILABLE", true
}
func respond(w http.ResponseWriter, id string, status int, data any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(map[string]any{"request_id": id, "data": data})
}
func (s *Server) fail(w http.ResponseWriter, id string, err error) {
	status, code, retry := errorStatus(err)
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	if status == 429 {
		w.Header().Set("Retry-After", "60")
	}
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(map[string]any{"request_id": id, "error": map[string]any{"code": code, "message": code, "retryable": retry}})
	s.Log.Info("request rejected", "request_id", id, "status", status, "code", code)
}
func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	id := store.UUID()
	// The deployed server must never accept a plaintext connection, including via a proxy.
	if r.TLS == nil {
		s.fail(w, id, errors.New("INVALID_REQUEST"))
		return
	}
	if r.URL.Path == "/health" && r.Method == "GET" {
		if err := s.Store.DB.PingContext(r.Context()); err != nil {
			s.fail(w, id, err)
		} else {
			respond(w, id, 200, map[string]bool{"ok": true})
		}
		return
	}
	if !strings.HasPrefix(r.URL.Path, "/api/v1/") {
		s.fail(w, id, store.ErrNotFound)
		return
	}
	if r.Method != "GET" {
		media, _, err := mime.ParseMediaType(r.Header.Get("Content-Type"))
		if err != nil || media != "application/json" || r.Header.Get("Content-Encoding") != "" {
			s.fail(w, id, errors.New("INVALID_REQUEST"))
			return
		}
	}
	path := strings.TrimPrefix(r.URL.Path, "/api/v1/")
	if path == "auth/register" || path == "auth/login" {
		if r.Method != "POST" {
			s.fail(w, id, errors.New("INVALID_REQUEST"))
			return
		}
		s.auth(w, r, id, path)
		return
	}
	authorization := r.Header.Get("Authorization")
	if !strings.HasPrefix(authorization, "Bearer ") {
		s.fail(w, id, store.ErrUnauthorized)
		return
	}
	principal, err := s.Store.Authenticate(r.Context(), strings.TrimPrefix(authorization, "Bearer "))
	if err != nil {
		s.fail(w, id, err)
		return
	}
	if strings.HasPrefix(path, "auth/") {
		s.account(w, r, id, path, principal)
		return
	}
	select {
	case s.slots <- struct{}{}:
		defer func() { <-s.slots }()
	default:
		s.fail(w, id, errors.New("BUSY"))
		return
	}
	if err = s.vault(w, r, id, path, principal); err != nil {
		s.fail(w, id, err)
	}
}
func control(r *http.Request, w http.ResponseWriter, fields ...string) (map[string]json.RawMessage, error) {
	r.Body = http.MaxBytesReader(w, r.Body, 64<<10)
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		return nil, err
	}
	obj, err := protocol.StrictObject(raw, fields...)
	if err != nil {
		return nil, err
	}
	if len(obj) != len(fields) {
		return nil, errors.New("INVALID_REQUEST")
	}
	return obj, nil
}
func textField(obj map[string]json.RawMessage, key string) (string, error) {
	var result string
	err := json.Unmarshal(obj[key], &result)
	return result, err
}
func (s *Server) auth(w http.ResponseWriter, r *http.Request, id, path string) {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	if !s.allow("ip:" + host) {
		s.fail(w, id, errors.New("RATE_LIMITED"))
		return
	}
	select {
	case s.authSlots <- struct{}{}:
		defer func() { <-s.authSlots }()
	default:
		s.fail(w, id, errors.New("BUSY"))
		return
	}
	if path == "auth/register" && !s.RegistrationEnabled {
		s.fail(w, id, errors.New("REGISTRATION_DISABLED"))
		return
	}
	obj, err := control(r, w, "username", "password")
	if err != nil {
		s.fail(w, id, err)
		return
	}
	username, err := textField(obj, "username")
	if err != nil {
		s.fail(w, id, err)
		return
	}
	password, err := textField(obj, "password")
	if err != nil {
		s.fail(w, id, err)
		return
	}
	normalized, err := protocol.NormalizeAccount(username)
	if err != nil {
		s.fail(w, id, errors.New("INVALID_CREDENTIALS"))
		return
	}
	if !s.allow("account:" + normalized) {
		s.fail(w, id, errors.New("RATE_LIMITED"))
		return
	}
	if path == "auth/register" {
		err = s.Store.Register(r.Context(), username, password)
		if err != nil {
			s.fail(w, id, err)
		} else {
			respond(w, id, 200, map[string]bool{"ok": true})
		}
		return
	}
	result, err := s.Store.Login(r.Context(), username, password)
	if err != nil {
		s.fail(w, id, err)
	} else {
		respond(w, id, 200, result)
	}
}
func (s *Server) account(w http.ResponseWriter, r *http.Request, id, path string, p store.Principal) {
	if r.Method != "POST" {
		s.fail(w, id, errors.New("INVALID_REQUEST"))
		return
	}
	var err error
	switch path {
	case "auth/logout":
		if _, err = control(r, w); err == nil {
			err = s.Store.Logout(r.Context(), p)
		}
	case "auth/password":
		if !s.allow("password:" + strconv.FormatInt(p.AccountID, 10)) {
			s.fail(w, id, errors.New("RATE_LIMITED"))
			return
		}
		select {
		case s.authSlots <- struct{}{}:
			defer func() { <-s.authSlots }()
		default:
			s.fail(w, id, errors.New("BUSY"))
			return
		}
		var obj map[string]json.RawMessage
		obj, err = control(r, w, "old_password", "new_password")
		if err == nil {
			var old, newPassword string
			old, err = textField(obj, "old_password")
			if err == nil {
				newPassword, err = textField(obj, "new_password")
			}
			if err == nil {
				err = s.Store.ChangePassword(r.Context(), p, old, newPassword)
			}
		}
	default:
		err = store.ErrNotFound
	}
	if err != nil {
		s.fail(w, id, err)
	} else {
		respond(w, id, 200, map[string]bool{"ok": true})
	}
}
