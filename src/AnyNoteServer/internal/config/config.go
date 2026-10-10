package config

import (
	"anynote/server/internal/protocol"
	"encoding/json"
	"errors"
	"io"
	"net"
	"os"
	"path/filepath"
)

type Config struct {
	Listen              string `json:"listen"`
	DataDir             string `json:"data_dir"`
	CertFile            string `json:"cert_file"`
	KeyFile             string `json:"key_file"`
	RegistrationEnabled bool   `json:"registration_enabled"`
	AccountVaults       int    `json:"account_vaults"`
	AccountBytes        int64  `json:"account_bytes"`
}

func Load(path string) (Config, error) {
	c := Config{Listen: "127.0.0.1:8443", AccountVaults: 100, AccountBytes: 1 << 30}
	f, err := os.Open(path)
	if err != nil {
		return c, err
	}
	defer f.Close()
	raw, err := io.ReadAll(io.LimitReader(f, (64<<10)+1))
	if err != nil {
		return c, err
	}
	if _, err = protocol.StrictObject(raw, "listen", "data_dir", "cert_file", "key_file", "registration_enabled", "account_vaults", "account_bytes"); err != nil {
		return c, err
	}
	if err = json.Unmarshal(raw, &c); err != nil {
		return c, err
	}
	if c.DataDir == "" || c.CertFile == "" || c.KeyFile == "" || c.AccountVaults < 1 || c.AccountBytes < 1 {
		return c, errors.New("invalid configuration")
	}
	if _, _, err = net.SplitHostPort(c.Listen); err != nil {
		return c, err
	}
	absolute, err := filepath.Abs(path)
	if err != nil {
		return c, err
	}
	dir := filepath.Dir(absolute)
	for _, p := range []*string{&c.DataDir, &c.CertFile, &c.KeyFile} {
		if !filepath.IsAbs(*p) {
			*p = filepath.Join(dir, *p)
		}
	}
	return c, nil
}
