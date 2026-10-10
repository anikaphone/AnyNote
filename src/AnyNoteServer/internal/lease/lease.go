package lease

import "os"

// Acquire protects a service data directory for the process lifetime. The OS
// releases the lock on a crash; the harmless lock file itself may remain.
func Acquire(path string) (*os.File, error) {
	file, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return nil, err
	}
	if err = lock(file); err != nil {
		file.Close()
		return nil, err
	}
	return file, nil
}
