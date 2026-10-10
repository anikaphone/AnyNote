package protocol

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

// RequestHash hashes a validated business descriptor. Use json.Decoder.UseNumber
// when reading fixture JSON; raw RTF, credentials and operation_id are forbidden.
func RequestHash(descriptor any) (string, error) {
	var out strings.Builder
	out.WriteString("AnyNote/request/v1\n")
	var encode func(any, int) error
	encode = func(value any, depth int) error {
		if depth > 16 || out.Len() > 192<<20 {
			return errors.New("LIMIT_EXCEEDED")
		}
		switch v := value.(type) {
		case nil:
			out.WriteByte('n')
		case bool:
			if v {
				out.WriteByte('t')
			} else {
				out.WriteByte('f')
			}
		case json.Number:
			n, err := strconv.ParseInt(string(v), 10, 64)
			if err != nil || strconv.FormatInt(n, 10) != string(v) {
				return errors.New("INVALID_INTEGER")
			}
			out.WriteByte('i')
			out.WriteString(string(v))
			out.WriteByte(';')
		case int:
			return encode(json.Number(strconv.Itoa(v)), depth)
		case int64:
			return encode(json.Number(strconv.FormatInt(v, 10)), depth)
		case string:
			if !utf8.ValidString(v) {
				return errors.New("INVALID_UTF8")
			}
			if len(v) > (192<<20)-out.Len() {
				return errors.New("LIMIT_EXCEEDED")
			}
			out.WriteByte('s')
			out.WriteString(strconv.Itoa(len(v)))
			out.WriteByte(':')
			out.WriteString(v)
		case []any:
			out.WriteByte('a')
			out.WriteString(strconv.Itoa(len(v)))
			out.WriteByte(':')
			for _, item := range v {
				if err := encode(item, depth+1); err != nil {
					return err
				}
			}
		case map[string]any:
			out.WriteByte('o')
			out.WriteString(strconv.Itoa(len(v)))
			out.WriteByte(':')
			keys := make([]string, 0, len(v))
			for key := range v {
				keys = append(keys, key)
			}
			sort.Strings(keys)
			for _, key := range keys {
				if key == "rtf_base64" || key == "password" || key == "token" || key == "operation_id" {
					return errors.New("INVALID_DESCRIPTOR")
				}
				if err := encode(key, depth+1); err != nil {
					return err
				}
				if err := encode(v[key], depth+1); err != nil {
					return err
				}
			}
		default:
			return errors.New("INVALID_DESCRIPTOR")
		}
		if out.Len() > 192<<20 {
			return errors.New("LIMIT_EXCEEDED")
		}
		return nil
	}
	if err := encode(descriptor, 0); err != nil {
		return "", err
	}
	digest := sha256.Sum256([]byte(out.String()))
	return hex.EncodeToString(digest[:]), nil
}
