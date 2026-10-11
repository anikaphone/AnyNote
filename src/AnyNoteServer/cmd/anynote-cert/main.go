package main

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"errors"
	"flag"
	"fmt"
	"math/big"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

func writeNew(path string, bytes []byte) error {
	f, err := os.OpenFile(path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	defer f.Close()
	if _, err = f.Write(bytes); err != nil {
		return err
	}
	return f.Sync()
}
func run() error {
	directory := flag.String("dir", "certs", "output directory; existing files are never overwritten")
	hosts := flag.String("hosts", "localhost,127.0.0.1", "comma-separated exact DNS names and IP addresses used by clients")
	uriStr := flag.String("uri", "", "optional service URL to embed in SAN (e.g. https://1.2.3.4:8443)")
	flag.Parse()
	if err := os.MkdirAll(*directory, 0700); err != nil {
		return err
	}
	for _, name := range []string{"server.crt", "server.key"} {
		if _, err := os.Stat(filepath.Join(*directory, name)); !errors.Is(err, os.ErrNotExist) {
			return errors.New("certificate/key already exists")
		}
	}
	key, err := rsa.GenerateKey(rand.Reader, 3072)
	if err != nil {
		return err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return err
	}
	now := time.Now().UTC()
	template := x509.Certificate{SerialNumber: serial, Subject: pkix.Name{CommonName: "AnyNote private sync"}, NotBefore: now.Add(-time.Hour), NotAfter: now.AddDate(100, 0, 0),
		KeyUsage: x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment | x509.KeyUsageCertSign, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, BasicConstraintsValid: true, IsCA: true}
	for _, host := range strings.Split(*hosts, ",") {
		host = strings.TrimSpace(host)
		if host == "" || strings.ContainsAny(host, "/\\\r\n ") {
			return errors.New("invalid SAN")
		}
		if ip := net.ParseIP(host); ip != nil {
			template.IPAddresses = append(template.IPAddresses, ip)
		} else {
			template.DNSNames = append(template.DNSNames, host)
		}
	}
	if *uriStr != "" {
		u, err := url.Parse(*uriStr)
		if err != nil || u.Scheme == "" || u.Host == "" {
			return errors.New("invalid URI")
		}
		template.URIs = append(template.URIs, u)
	}
	certificate, err := x509.CreateCertificate(rand.Reader, &template, &template, &key.PublicKey, key)
	if err != nil {
		return err
	}
	if err = writeNew(filepath.Join(*directory, "server.key"), pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(key)})); err != nil {
		return err
	}
	return writeNew(filepath.Join(*directory, "server.crt"), pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certificate}))
}
func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println("Created 100-year certificate. Distribute server.crt only; never distribute server.key.")
}
