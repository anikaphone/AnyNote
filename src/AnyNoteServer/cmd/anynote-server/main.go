package main

import (
	"anynote/server/internal/api"
	"anynote/server/internal/config"
	"anynote/server/internal/lease"
	"anynote/server/internal/store"
	"context"
	"crypto/tls"
	"crypto/x509"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"syscall"
	"time"
)

func run() error {
	path := flag.String("config", "server.json", "configuration file")
	flag.Parse()
	c, err := config.Load(*path)
	if err != nil {
		return err
	}
	certificate, err := tls.LoadX509KeyPair(c.CertFile, c.KeyFile)
	if err != nil {
		return err
	}
	leaf, err := x509.ParseCertificate(certificate.Certificate[0])
	if err != nil {
		return err
	}
	now := time.Now()
	if now.Before(leaf.NotBefore) || now.After(leaf.NotAfter) || len(leaf.DNSNames)+len(leaf.IPAddresses) == 0 {
		return os.ErrInvalid
	}
	if err = os.MkdirAll(c.DataDir, 0700); err != nil {
		return err
	}
	if runtime.GOOS != "windows" {
		for _, path := range []string{c.KeyFile, c.DataDir} {
			info, err := os.Stat(path)
			if err != nil {
				return err
			}
			if info.Mode().Perm()&0077 != 0 {
				return os.ErrPermission
			}
		}
	}
	ownership, err := lease.Acquire(filepath.Join(c.DataDir, ".server.lock"))
	if err != nil {
		return err
	}
	defer ownership.Close()
	db, err := store.Open(filepath.Join(c.DataDir, "anynote-server.sqlite"))
	if err != nil {
		return err
	}
	defer db.Close()
	db.AccountVaults = c.AccountVaults
	db.AccountBytes = c.AccountBytes
	server := &http.Server{Addr: c.Listen, Handler: api.New(db, c.RegistrationEnabled), ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout: 10 * time.Minute, WriteTimeout: 10 * time.Minute, IdleTimeout: 60 * time.Second, MaxHeaderBytes: 16 << 10,
		TLSConfig: &tls.Config{MinVersion: tls.VersionTLS12, Certificates: []tls.Certificate{certificate}}}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		defer cancel()
		_ = server.Shutdown(shutdown)
	}()
	slog.Info("HTTPS server starting", "listen", c.Listen)
	err = server.ListenAndServeTLS("", "")
	if err == http.ErrServerClosed {
		return nil
	}
	return err
}
func main() {
	if err := run(); err != nil {
		slog.Error("server stopped", "error", err)
		os.Exit(1)
	}
}
