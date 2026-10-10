package protocol

import (
	"errors"
	"math"
	"sort"
	"strconv"
)

func Descendants(nodes []Node, root string) map[string]bool {
	children := map[string][]string{}
	for _, n := range nodes {
		if n.ParentSyncID != nil {
			children[*n.ParentSyncID] = append(children[*n.ParentSyncID], n.SyncID)
		}
	}
	result := map[string]bool{}
	pending := []string{root}
	for len(pending) > 0 {
		id := pending[len(pending)-1]
		pending = pending[:len(pending)-1]
		if result[id] {
			continue
		}
		result[id] = true
		pending = append(pending, children[id]...)
	}
	return result
}
func index(nodes []Node) map[string]Node {
	r := map[string]Node{}
	for _, n := range nodes {
		r[n.SyncID] = n
	}
	return r
}
func parent(n Node) string {
	if n.ParentSyncID == nil {
		return ""
	}
	return *n.ParentSyncID
}
func Subtree(source Snapshot, root string) (Snapshot, error) {
	idx := index(source.Nodes)
	n, ok := idx[root]
	if !ok {
		return Snapshot{}, errors.New("NOT_FOUND")
	}
	selected := Descendants(source.Nodes, root)
	for n.ParentSyncID != nil {
		n = idx[*n.ParentSyncID]
		selected[n.SyncID] = true
	}
	result := source
	result.Nodes = []Node{}
	for _, node := range source.Nodes {
		if selected[node.SyncID] {
			result.Nodes = append(result.Nodes, node)
		}
	}
	return result, nil
}
func PlanSubtree(target, source Snapshot, root string, limits Limits) (Snapshot, error) {
	if source.UID != target.UID {
		return Snapshot{}, errors.New("UID_MISMATCH")
	}
	if err := ValidateTree(source.Nodes, limits); err != nil {
		return Snapshot{}, err
	}
	if err := ValidateTree(target.Nodes, limits); err != nil {
		return Snapshot{}, err
	}
	incoming, existing := index(source.Nodes), index(target.Nodes)
	node, ok := incoming[root]
	if !ok {
		return Snapshot{}, errors.New("NOT_FOUND")
	}
	selected := Descendants(source.Nodes, root)
	replaced := map[string]bool{}
	if _, ok := existing[root]; ok {
		replaced = Descendants(target.Nodes, root)
	}
	for id := range selected {
		if _, ok := existing[id]; !ok || replaced[id] {
			continue
		}
		for child := range Descendants(target.Nodes, id) {
			if !selected[child] && !replaced[child] {
				return Snapshot{}, errors.New("OUTSIDE_SUBTREE")
			}
		}
	}
	for node.ParentSyncID != nil {
		node = incoming[*node.ParentSyncID]
		if _, ok := existing[node.SyncID]; !ok {
			selected[node.SyncID] = true
		}
	}
	result := target
	result.Nodes = []Node{}
	var next int64
	affected := map[string]bool{}
	for _, n := range target.Nodes {
		id, _ := strconv.ParseInt(n.SourceID, 10, 64)
		next = max(next, id)
		if !selected[n.SyncID] && !replaced[n.SyncID] {
			result.Nodes = append(result.Nodes, n)
		} else {
			affected[parent(n)] = true
		}
	}
	keys := []string{}
	for id := range selected {
		keys = append(keys, id)
	}
	sort.Strings(keys)
	for _, id := range keys {
		n := incoming[id]
		if old, ok := existing[id]; ok {
			n.SourceID = old.SourceID
		} else {
			if next == math.MaxInt64 {
				return Snapshot{}, errors.New("ID_EXHAUSTED")
			}
			next++
			n.SourceID = strconv.FormatInt(next, 10)
		}
		affected[parent(n)] = true
		result.Nodes = append(result.Nodes, n)
	}
	for p := range affected {
		siblings := []*Node{}
		for i := range result.Nodes {
			if parent(result.Nodes[i]) == p {
				siblings = append(siblings, &result.Nodes[i])
			}
		}
		sort.SliceStable(siblings, func(i, j int) bool {
			a, b := siblings[i], siblings[j]
			if a.Sequence != b.Sequence {
				return a.Sequence < b.Sequence
			}
			if selected[a.SyncID] != selected[b.SyncID] {
				return selected[a.SyncID]
			}
			ai, _ := strconv.ParseInt(a.SourceID, 10, 64)
			bi, _ := strconv.ParseInt(b.SourceID, 10, 64)
			return ai < bi
		})
		for i, n := range siblings {
			n.Sequence = int32(i)
		}
	}
	return result, ValidateTree(result.Nodes, limits)
}
