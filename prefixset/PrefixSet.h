#include <arpa/inet.h>
#include <sys/socket.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

template <int AddrFamily> struct AddrUtil { };
template <> struct AddrUtil<AF_INET> {
  static const int kByteLen = 4;
  typedef struct in_addr AddrType;
};
template <> struct AddrUtil<AF_INET6> {
  static const int kByteLen = 16;
  typedef struct in6_addr AddrType;
};

template <int AddrFamily>
class PrefixSet {
 private:
  struct Node;

  bool parsePrefix(const std::string& prefix, char* bytes, int* prefixLen) {
    size_t slashPos = prefix.find('/');
    if (slashPos == std::string::npos) {
      return false;
    }
    std::string ip = prefix.substr(0, slashPos);
    std::string sPrefixLen = prefix.substr(slashPos + 1);
    return (sscanf(sPrefixLen.c_str(), "%u", prefixLen) == 1 &&
            inet_pton(AddrFamily, ip.c_str(), bytes) == 1);
  }

 public:
  PrefixSet() {
  }

  PrefixSet(const PrefixSet& rhs) {
    copyRecur(&root_, &rhs.root_);
  }

  PrefixSet& operator=(const PrefixSet& rhs) {
    copyRecur(&root_, &rhs.root_);
    return *this;
  }

  bool add(const std::string& prefix) {
    char bytes[AddrUtil<AddrFamily>::kByteLen];
    int prefixLen;
    if (!parsePrefix(prefix, bytes, &prefixLen)) {
      return false;
    }

    std::vector<Node*> parentChain;
    Node *node = &root_;
    for (int i = 0; i < prefixLen; i++) {
      if (node->terminal) {
        // Parent prefix already there
        return true;
      }

      parentChain.push_back(node);

      int bit = (bytes[i / 8] >> (7 - (i % 8))) & 1;
      if (node->children[bit] == NULL) {
        node->children[bit] = new Node;
      }
      node = node->children[bit];
    }
    node->setTerminal();
    
    for (int i = parentChain.size() - 1; i >= 0; i--) {
      Node* node = parentChain[i];
      if (!node->canCoalesce()) {
        break;
      }
      node->setTerminal();
    }
    return true;
  }

  bool remove(const std::string& prefix) {
    char bytes[AddrUtil<AddrFamily>::kByteLen];
    int prefixLen;
    if (!parsePrefix(prefix, bytes, &prefixLen)) {
      return false;
    }

    Node *parent = NULL;
    Node *node = &root_;
    for (int i = 0; i < prefixLen; i++) {
      int bit = (bytes[i / 8] >> (7 - (i % 8))) & 1;

      if (node->terminal) {
        node->terminal = false;
        for (int i = 0; i < 2; i++) {
          node->children[i] = new Node;
          node->children[i]->setTerminal();
        }
      }
      parent = node;
      node = parent->children[bit];
      if (!node) {
        return true;
      }
    }
    for (int i = 0; i < 2; i++) {
      if (parent->children[i] == node) {
        delete node;
        parent->children[i] = NULL;
        break;
      }
    }
  }

  std::vector<std::string> getAll() {
    char bytes[AddrUtil<AddrFamily>::kByteLen] = {0};
    std::vector<std::string> out;
    getAllRecur(&root_, bytes, 0, &out);
    return out;
  }

  void clear() {
    clearRecur(&root_);
  }

 private:
  void getAllRecur(Node* node, char* bytes, int prefixLen,
                   std::vector<std::string>* out) {
    if (!node) {
      return;
    }
    if (node->terminal) {
      assert(node->children[0] == NULL && node->children[1] == NULL);
      char buf[INET6_ADDRSTRLEN];
      const char* ret = inet_ntop(AddrFamily, bytes, buf, sizeof(buf));
      assert(ret != NULL);
      char wholePrefix[INET6_ADDRSTRLEN + 10];
      snprintf(wholePrefix, sizeof(wholePrefix), "%s/%d", ret, prefixLen);
      out->push_back(std::string(wholePrefix));
      return;
    }
    assert(node == &root_ || node->children[0] || node->children[1]);

    getAllRecur(node->children[0], bytes, prefixLen + 1, out);

    int b = 1 << (7 - (prefixLen % 8));
    bytes[prefixLen / 8] |= b;
    getAllRecur(node->children[1], bytes, prefixLen + 1, out);
    bytes[prefixLen / 8] &= ~b;
  }

  void clearRecur(Node* node) {
    if (node != NULL) {
      clearRecur(node->children[0]);
      delete node->children[0];
      clearRecur(node->children[1]);
      delete node->children[1];
      node->children[0] = node->children[1] = NULL;
    }
  }

  void copyRecur(Node* lhs, const Node* rhs) {
    assert(lhs != NULL && rhs != NULL);
    lhs->terminal = rhs->terminal;
    for (int i = 0; i < 2; i++) {
      if (rhs->children[i]) {
        lhs->children[i] = new Node;
        copyRecur(lhs->children[i], rhs->children[i]);
      }
    }
  }

  struct Node {
    Node() {
      children[0] = children[1] = NULL;
      terminal = false;
    }
    bool canCoalesce() {
      return (children[0] != NULL && children[1] != NULL &&
              children[0]->terminal && children[1]->terminal);
    }
    void setTerminal() {
      delete children[0];
      delete children[1];
      children[0] = children[1] = NULL;
      terminal = true;
    }
    Node *children[2];
    bool terminal;
  };
  Node root_;
};
