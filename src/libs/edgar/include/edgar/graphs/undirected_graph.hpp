#pragma once

#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace edgar::graphs {

template <typename T>
class UndirectedAdjacencyListGraph {
public:
    bool is_directed() const { return false; }

    void add_vertex(const T& vertex) {
        if (adj_.count(vertex)) {
            throw std::invalid_argument("Vertex already exists");
        }
        adj_[vertex] = {};
    }

    void add_edge(const T& from, const T& to) {
        auto it_f = adj_.find(from);
        auto it_t = adj_.find(to);
        if (it_f == adj_.end() || it_t == adj_.end()) {
            throw std::invalid_argument("One of the vertices does not exist");
        }
        auto& from_list = it_f->second;
        auto& to_list = it_t->second;
        for (const auto& n : from_list) {
            if (n == to) {
                throw std::invalid_argument("The edge was already added");
            }
        }
        from_list.push_back(to);
        to_list.push_back(from);
    }

    std::vector<T> neighbours(const T& vertex) const {
        auto it = adj_.find(vertex);
        if (it == adj_.end()) {
            throw std::invalid_argument("The vertex does not exist");
        }
        return it->second;
    }

    bool has_edge(const T& from, const T& to) const {
        for (const auto& n : neighbours(from)) {
            if (n == to) {
                return true;
            }
        }
        return false;
    }

    std::vector<T> vertices() const {
        std::vector<T> out;
        out.reserve(adj_.size());
        for (const auto& kv : adj_) {
            out.push_back(kv.first);
        }
        return out;
    }

    std::size_t vertex_count() const { return adj_.size(); }

private:
    std::map<T, std::vector<T>> adj_;
};

} // namespace edgar::graphs
