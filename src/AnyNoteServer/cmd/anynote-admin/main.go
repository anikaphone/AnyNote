package main

import (
	"anynote/server/internal/store"
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"time"
)

func run() error {
	source := flag.String("source", "", "source database or backup")
	destination := flag.String("destination", "", "new destination database file (must not exist)")
	restore := flag.Bool("restore", false, "restore into a NEW directory; revoke sessions and renew epochs")
	flag.Parse()
	if *source == "" || *destination == "" {
		return errors.New("source and destination are required")
	}
	if _, err := os.Stat(*source); err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	if *restore {
		return store.RestoreInto(ctx, *source, *destination)
	}
	db, err := store.Open(*source)
	if err != nil {
		return err
	}
	defer db.Close()
	return db.Backup(ctx, *destination)
}
func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println("Verified backup/restore completed. Restore requires a stopped service before switching data_dir.")
}
