// p0probe is an isolated TLS test fixture, not the production sync service.
package main

import (
	"anynote/server/internal/api"
	"anynote/server/internal/store"
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/json"
	"encoding/pem"
	"flag"
	"fmt"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"
)

func certificate(dir, name, hostname string, expired bool) (tls.Certificate, error) {
	key, err := rsa.GenerateKey(rand.Reader, 3072)
	if err != nil {
		return tls.Certificate{}, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return tls.Certificate{}, err
	}
	now := time.Now()
	template := x509.Certificate{SerialNumber: serial, Subject: pkix.Name{CommonName: hostname},
		NotBefore: now.Add(-time.Hour), NotAfter: now.AddDate(100, 0, 0), DNSNames: []string{hostname},
		KeyUsage:    x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, BasicConstraintsValid: true}
	if hostname == "localhost" {
		template.IPAddresses = []net.IP{net.ParseIP("127.0.0.1")}
	}
	if expired {
		template.NotBefore = now.AddDate(-2, 0, 0)
		template.NotAfter = now.Add(-time.Hour)
	}
	der, err := x509.CreateCertificate(rand.Reader, &template, &template, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(key)})
	// Refuse reuse/overwrite of any existing fixture material.
	f, err := os.OpenFile(filepath.Join(dir, name+".crt"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return tls.Certificate{}, err
	}
	if _, err = f.Write(certPEM); err != nil {
		f.Close()
		return tls.Certificate{}, err
	}
	if err = f.Close(); err != nil {
		return tls.Certificate{}, err
	}
	// Private keys stay in this process; they are not needed by the client.
	return tls.X509KeyPair(certPEM, keyPEM)
}

func run() error {
	dir := flag.String("dir", "", "new isolated fixture directory")
	flag.Parse()
	if *dir == "" {
		return fmt.Errorf("-dir is required")
	}
	if err := os.MkdirAll(*dir, 0700); err != nil {
		return err
	}
	addresses := map[string]string{}
	database, err := store.Open(filepath.Join(*dir, "fixture.sqlite"))
	if err != nil {
		return err
	}
	defer database.Close()
	service := api.New(database, true)
	var dropNext atomic.Bool
	for _, name := range []string{"valid", "wrong-host", "expired"} {
		host := "localhost"
		if name == "wrong-host" {
			host = "invalid.example"
		}
		cert, err := certificate(*dir, name, host, name == "expired")
		if err != nil {
			return err
		}
		listener, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return err
		}
		defer listener.Close()
		addresses[name] = "https://" + listener.Addr().String()
		mux := http.NewServeMux()
		mux.HandleFunc("/api/v1/fixture/drop-next-response", func(w http.ResponseWriter, r *http.Request) {
			dropNext.Store(true)
			w.Header().Set("Content-Type", "application/json")
			_ = json.NewEncoder(w).Encode(map[string]any{"request_id": store.UUID(), "data": map[string]bool{"ok": true}})
		})
		mux.HandleFunc("/api/v1/", func(w http.ResponseWriter, r *http.Request) {
			if (r.Method == "PUT" || r.Method == "PATCH" || r.Method == "DELETE" || (r.Method == "POST" && strings.HasSuffix(r.URL.Path, "/sync-batch"))) && dropNext.Swap(false) {
				recorded := httptest.NewRecorder()
				service.ServeHTTP(recorded, r)
				if recorded.Code == 200 {
					if hijacker, ok := w.(http.Hijacker); ok {
						connection, _, err := hijacker.Hijack()
						if err == nil {
							connection.Close()
							return
						}
					}
				}
				for key, values := range recorded.Header() {
					w.Header()[key] = values
				}
				w.WriteHeader(recorded.Code)
				_, _ = w.Write(recorded.Body.Bytes())
				return
			}
			service.ServeHTTP(w, r)
		})
		mux.HandleFunc("/ok", func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "application/json")
			fmt.Fprint(w, `{"ok":true}`)
		})
		mux.HandleFunc("/redirect", func(w http.ResponseWriter, r *http.Request) { http.Redirect(w, r, "http://127.0.0.1:1", 302) })
		mux.HandleFunc("/slow", func(w http.ResponseWriter, r *http.Request) {
			select {
			case <-r.Context().Done():
			case <-time.After(5 * time.Second):
			}
		})
		server := &http.Server{Handler: mux, ReadHeaderTimeout: 5 * time.Second,
			ReadTimeout: 10 * time.Minute, WriteTimeout: 10 * time.Minute, IdleTimeout: 10 * time.Second,
			TLSConfig: &tls.Config{MinVersion: tls.VersionTLS12, Certificates: []tls.Certificate{cert}}}
		defer server.Close()
		go server.Serve(tls.NewListener(listener, server.TLSConfig))
	}
	bytes, err := json.Marshal(addresses)
	if err != nil {
		return err
	}
	f, err := os.OpenFile(filepath.Join(*dir, "ready.tmp"), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	if _, err = f.Write(bytes); err != nil {
		f.Close()
		return err
	}
	if err = f.Close(); err != nil {
		return err
	}
	if err = os.Rename(filepath.Join(*dir, "ready.tmp"), filepath.Join(*dir, "ready.json")); err != nil {
		return err
	}
	select {}
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
