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
当表变得太满的时候，insertion子路径也会触发调整大小
hm_insert和hm_start_resizing一起实现了哈希表的插入操作，并在需要时启动哈希表的调整大小操作，以确保哈希表维护在合适的负载因子下，以提高性能和避免过度填充。
```c++
const size_t k_max_load_factor = 8; //最大负载因子

void hm_insert(HMap* hmap, HNode* node){
	if(!hmap->ht1.tab){
		h_init(&hmap->ht1, 4);
	}
	h_insert(&hmap->ht1, node);

	if(!hmap->ht2.tab){
		size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1); //mask=n-1，mask+1也就是ht1.tab的容量
		if(load_factor >= k_max_load_factor){
			hm_start_resizing(hmap);
		}
	}
	hm_help_resizing(hmap);
}

static void hm_start_resizing(HMap* hmap){
	assert(hmap->ht2.tab == NULL);
	hmap->ht2 = hmap->ht1; //将ht1的内容迁移到ht2中
	h_init(&hmap->ht1, (hmap->ht1.mask + 1)*2); //然后将ht1初始化并扩充容量为原来两倍
	hmap->resizing_pos = 0;
}
```
然后就是将一个key在表中删除的操作，没啥需要注意的
先在ht1里边找，找不到在ht2里边找。
```c++
HNode* hm_pop(HMap* hmap, HNode* key, bool (*cmp)(HNode*, HNode*)){
	hm_help_resizing(hmap);
	HNode** from = h_lookup(hmap->ht1, key, cmp);
	if(from){
		return h_detach(&hmap->ht1, from);	
	}
	from = h_lookup(hmap->ht2, key, cmp);
	if(from){
		return h_detach(&hmap->ht2, from);
	}
	return NULL;
}
```
# 侵入式数据结构
概念：1.它本身作为数据结构的一部分包含指向自身类型的指针。这就是所谓的"侵入性"，即数据结构的定义包含指向自身的指针（比如链表）。2.经常使用外部指针来操作已存在的数据结构。
首先定义一个结构体Entry，注意Entry中有类型为HNode的参数，而HNode的结构中就包含指向HNode的指针
```c++
struct Entry{
	struct HNode node;
	std::string key;
	std::string val;
}
```
然后就是一个do_get函数（这段代码使用了相当多的类型转换）
函数逻辑：首先得到key值并计算哈希值，然后再在哈希表中查找这个哈希值，找到了就将这个节点复制到res中
```c++
static struct{
	HMap db;
}g_data;

static unit32_t do_get(
std::vector<string> &cmp, uint8_t *res, uint32_t *reslen)
{
	Entry key;
	key.key.swap(cmd[1]); //将key.key与cmd[1]交换
	key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size()); //计算key.key.data并得到哈希码

	HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
	if(!node){
		return RES_NX;
	}

	const std::string &val = container_of(node, Entry, node)->val;
	assert(val.size() <= k_max_msg);
	memcpy(res, val.data(), val.size());
	*reslen = (uint32_t)val.size();
	return RES_OK;
}

//比较两个哈希表节点是否相等
static bool entry_eq(HNode *lhs, HNode *rhs){
	struct Entry *le = container_of(lhs, struct Entry, node);
	struct Entry *re = container_of(rhs, struct Entry, node);
	return lhs->hcode == rhs->hcode && le->key == re->key;
}
```
注意：C++提供了好几种类型转换方法，分别是static/dynamic/const/reinterpret/c-style cast，除了最后两个都挺安全的，但是这里是用的是C-style转换。
然后是一个宏：
这是一个常见的C宏定义，通常用于实现在C结构体内包含其他结构体的情况下，从内部结构体指针获取包含它的外部结构体的指针。这通常在操作系统内核编程或底层数据结构中用于管理数据结构之间的嵌套关系。
```c++
#define container_of(ptr, type, member)({
	const typeof( ((type *)0)->member)*__mptr = (ptr);
	(type *)((char *)__mptr-offsetof(type,member));
})
//先是创建一个0指针，然后强制转换成(type*)，然后获得其中的member的信息
//然后创建一个指向member类型的指针，将ptr赋值给它
//然后使用offsetof宏来获取member在外部结构体type中的偏移量，转为char*并减去偏移量，再重新转换为type*类型
```
总的来说，这个宏允许你从内部结构体的指针找到包含它的外部结构体的指针，而不需要事先知道外部结构体的地址或结构。这在许多低级编程任务中非常有用，例如在操作系统内核中管理数据结构或在底层硬件编程中处理数据结构嵌套的情况。