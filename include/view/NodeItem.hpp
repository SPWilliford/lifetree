#ifndef NODEITEM_HPP
#define NODEITEM_HPP

#include <glibmm/object.h>
#include <glibmm/refptr.h>

// A node id boxed as a GObject, so it can live in a Gio::ListStore. Holds
// nothing else: title and decoration are read from the controllers at bind
// time, so there's no copy to go stale.
class NodeItem : public Glib::Object {
public:
    static Glib::RefPtr<NodeItem> create(int node_id) {
        return Glib::make_refptr_for_instance<NodeItem>(new NodeItem(node_id));
    }

    int node_id() const { return m_node_id; }

protected:
    explicit NodeItem(int node_id) : m_node_id(node_id) {}

private:
    int m_node_id;
};

#endif
