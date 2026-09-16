#ifndef NODEITEM_HPP
#define NODEITEM_HPP

#include <glibmm/object.h>
#include <glibmm/refptr.h>

// A node id, boxed so it can live in a Gio::ListStore — GTK's list models
// hold GObjects and an int isn't one.
//
// Stores nothing else on purpose: title, color and markers are read from
// the controllers at bind time, so there's no copy here to go stale.
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
