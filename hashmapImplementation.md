### 定义
```C++
// hashtable node, should be embedded into the payload
struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;
};

// a simple fixed-sized hashtable
struct HTab {
    HNode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};
```
### 初始化，插入和删除
```C++
// n must be a power of 2
static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0); //位运算检查2次幂，这是哈希表最佳大小选择，以便哈希函数可以均匀分布数据
    htab->tab = (HNode **)calloc(sizeof(HNode *), n);//tab将存储一个指向存储桶的指针数组。
    htab->mask = n - 1;//mask通常用于实现哈希函数中的位运算,用于在对哈希表进行索引操作时执行按位AND操作。通常将其设置为n - 1，因为n是2的幂，这意味着n - 1的所有位都被设置为1。以确保计算出的哈希值在数组大小范围内。
    htab->size = 0;
}

// hashtable insertion
// 注意，这个插入是将新节点插入链表的头部
static void h_insert(HTab *htab, HNode *node) {
    size_t pos = node->hcode & htab->mask;//mask是掩码，用于定位，hcode是哈希码
    HNode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

// hashtable look up subroutine.
// Pay attention to the return value. It returns the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
// 这个鬼二级指针搞得我真难看明白
static HNode **h_lookup(
    HTab *htab, HNode *key, bool (*cmp)(HNode *, HNode *))
{
    if (!htab->tab) {
        return NULL;
    }

    size_t pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[pos];
    while (*from) {
        if (cmp(*from, key)) {
            return from;
        }
        from = &(*from)->next; //这里等同于form=&((*form)->next)
    }
    return NULL;
}

// remove a node from the chain
// 这里不用二级指针是因为不用给form赋值了
static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = (*from)->next;
    htab->size--;
    return node;
}
```
# 渐进式调整大小
此时哈希表大小是固定的，在元素太多时需要迁移到一个更大的哈希表中，这是在redis中的哈希表需要的额外考虑。调整哈希表需要移动大量的节点到一个新表里，这会使服务器停滞一段时间。但是这种情况可以被避免，只要不是一次性移动所有东西就行，接下来我要通过两个哈希表来逐渐在他们之中移动节点来实现这个操作。
```c++
struct HMap{
	HTab ht1;
	HTab ht2;
	size_t resizing_pos = 0; //用于跟踪哈希表的大小变化，指示何时对哈希表进行渐进式的调整大小
}
```
一个lookup子路径将会帮助调整大小
`hm_lookup` 函数用于在 `HMap` 数据结构中的两个哈希表中查找指定的键，首先在第一个哈希表中查找，然后在第二个哈希表中查找，如果找到匹配的键，则返回指向匹配键的指针，否则返回 `NULL`。在查找之前，还会检查是否需要进行哈希表的渐进式调整大小。
```c++
HNode *hm_lookup(HMap* hmap, HNode* key, bool (*cmp)(HNode*, HNode*)){
	hm_help_resizing(hmap);
	HNode **from = h_lookup(&hmap->ht1, key, cmp);
	if(!from){
		from = h_lookup(&hmap->ht2, key, cmp);
	}
	return from ? *from : NULL;
}
```
hm_help_resizing函数也是一个帮助调整大小的子路径
`hm_help_resizing` 函数用于协助哈希表的调整大小操作。它会逐渐将节点从 `ht2` 迁移到 `ht1`，直到满足指定的条件。这种渐进式的调整大小策略有助于平滑地处理哈希表的容量调整，以提高性能。
```c++
const size_t k_resizing_work = 128;

static void hm_help_resizing(HMap *hmap){
	if(hmap->ht2 == NULL){
		return;
	}

	size_t nwork = 0;
	while(nwork < k_resizing_work && hmap->ht2.size > 0){
		HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
		if(!*from){
			hmap->resizing_pos++;
			continue;
		}

		h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
		nwork++;
	}

	if(hmap->ht2.size == 0){
		//done
		free(hmap->ht2.tab);
		hmap->ht2 = HTab{};
	}
}
```

