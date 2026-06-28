#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>

// ============================================================
//  Trie — 前缀树，用于英文关键字推荐索引
// ============================================================
// 每个节点代表一个字符，携带命中该前缀的词典行号集合。
// 支持: 构建(insert)、插入节点(insertNode)、查询(search)、序列化/反序列化

class Trie
{
public:
    // 构建阶段: 插入一个词，沿途所有前缀节点都记录该行号
    // e.g. insert("apple", 5) → a/ap/app/appl/apple 节点都记录行号 5
    void insert(const std::string& word, int lineNo) {
        Node* node = &_root;
        for (char c : word) {
            if (!node->children.count(c)) {
                node->children[c] = std::make_unique<Node>();
            }
            node = node->children[c].get();
            node->lineNos.insert(lineNo);
        }
    }

    // 以一个指定前缀 + 行号集合，精确插入叶子节点
    //（用于加载去冗后的前缀集）
    void insertNode(const std::string& prefix, const std::set<int>& lines) {
        Node* node = &_root;
        for (char c : prefix) {
            if (!node->children.count(c)) {
                node->children[c] = std::make_unique<Node>();
            }
            node = node->children[c].get();
        }
        node->lineNos = lines;
    }

    // 查询: 返回以 prefix 为前缀的候选词行号集合
    // 若 prefix 无法完整匹配 → 逐级回退，返回最长有效前缀的行号
    std::set<int> search(const std::string& prefix) const {
        const Node* node = &_root;
        const Node* lastValid = &_root;

        for (size_t i = 0; i < prefix.size(); ++i) {
            auto it = node->children.find(prefix[i]);
            if (it == node->children.end()) {
                return lastValid->lineNos;  // 回退到最近有效前缀
            }
            node = it->second.get();
            if (!node->lineNos.empty()) {
                lastValid = node;
            }
        }
        return node->lineNos.empty() ? lastValid->lineNos : node->lineNos;
    }

    // ========== 序列化（离线写文件用）==========
    void serialize(std::ostream& os) const {
        serializeNode(&_root, os);
    }

    // ========== 反序列化（在线加载用）==========
    void deserialize(std::istream& is) {
        deserializeNode(&_root, is);
    }

private:
    struct Node {
        std::map<char, std::unique_ptr<Node>> children;
        std::set<int> lineNos;
    };

    Node _root;

    // 递归写一个节点及子树
    // 格式: [int32:行号数量][int32×N:行号][int32:子节点数量]
    //        [char:字符] + 递归写子节点 ...
    static void serializeNode(const Node* node, std::ostream& os) {
        int32_t lc = static_cast<int32_t>(node->lineNos.size());
        os.write(reinterpret_cast<const char*>(&lc), 4);
        for (int lineNo : node->lineNos) {
            int32_t ln = static_cast<int32_t>(lineNo);
            os.write(reinterpret_cast<const char*>(&ln), 4);
        }

        int32_t cc = static_cast<int32_t>(node->children.size());
        os.write(reinterpret_cast<const char*>(&cc), 4);
        for (const auto& [c, child] : node->children) {
            os.put(c);
            serializeNode(child.get(), os);
        }
    }

    // 递归读一个节点及子树
    static void deserializeNode(Node* node, std::istream& is) {
        int32_t lc = 0;
        is.read(reinterpret_cast<char*>(&lc), 4);
        for (int32_t i = 0; i < lc; ++i) {
            int32_t ln = 0;
            is.read(reinterpret_cast<char*>(&ln), 4);
            node->lineNos.insert(static_cast<int>(ln));
        }

        int32_t cc = 0;
        is.read(reinterpret_cast<char*>(&cc), 4);
        for (int32_t i = 0; i < cc; ++i) {
            char c;
            is.get(c);
            auto child = std::make_unique<Node>();
            deserializeNode(child.get(), is);
            node->children[c] = std::move(child);
        }
    }
};
