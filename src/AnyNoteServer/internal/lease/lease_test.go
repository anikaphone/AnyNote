package lease

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestExclusiveProcessLease(t *testing.T) {
	if path := os.Getenv("ANYNOTE_LOCK_CHILD"); path != "" {
		if lock, err := Acquire(path); err == nil {
			lock.Close()
			t.Fatal("child bypassed lock")
		}
		return
	}
	path := filepath.Join(t.TempDir(), "server.lock")
	lock, err := Acquire(path)
	if err != nil {
		t.Fatal(err)
	}
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	command := exec.Command(executable, "-test.run=^TestExclusiveProcessLease$")
	command.Env = append(os.Environ(), "ANYNOTE_LOCK_CHILD="+path)
	if output, err := command.CombinedOutput(); err != nil {
		t.Fatalf("child: %s %v", output, err)
	}
	if err = lock.Close(); err != nil {
		t.Fatal(err)
	}
	again, err := Acquire(path)
	if err != nil {
		t.Fatal(err)
	}
	again.Close()
}
