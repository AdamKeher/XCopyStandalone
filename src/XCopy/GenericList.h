#ifndef GENERICLIST_H
#define GENERICLIST_H

template <class T> class Node
{
  public:
    T *data;
    Node *next;

    Node(T *item): data(item), next(nullptr) { };
};

template <class T> class GenericList
{
public:   
  GenericList() {
    head = nullptr;
    tail = nullptr;
  }

  ~GenericList() {
    Node<T> *p = head;
    Node<T> *prev = nullptr;
    while (p) {
      delete p->data;
      prev = p;
      p = p->next;
      delete prev;
    }
  }

  Node<T> *head, *tail;

  void add(T *item) {
    if (head == nullptr)
    {
      head = new Node<T>(item);
      tail = head;
    } else {
      Node<T> *temp = new Node<T>(item);
      tail->next = temp;
      tail = temp;
    }
    _size++;
  }

  long size() { return _size; }
private:
  long _size = 0;
};

#endif // GENERICLIST_H