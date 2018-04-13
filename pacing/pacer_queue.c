#include "pacer_queue.h"

static void pacer_free_packet_event(skiplist_item_t key, skiplist_item_t val, void* args)
{
	packet_event_t* ev = val.ptr;
	if (ev != NULL){
		free(ev);
	}
}

void pacer_queue_init(pacer_queue_t* que)
{
	que->oldest_ts = -1;
	que->total_size = 0;
	que->cache = skiplist_create(idu32_compare, pacer_free_packet_event, NULL);
	que->l = create_list();
}

void pacer_queue_destroy(pacer_queue_t* que)
{
	if (que != NULL){
		if (que->cache){
			skiplist_destroy(que->cache);
			que->cache = NULL;
		}

		if (que->l != NULL){
			destroy_list(que->l);
			que->l = NULL;
		}
	}
}

int pacer_queue_push(pacer_queue_t* que, packet_event_t* ev)
{
	int ret = -1;
	skiplist_iter_t* iter;
	skiplist_item_t key, val;

	packet_event_t* packet;

	key.u32 = ev->seq;
	iter = skiplist_search(que->cache, key);
	if (iter == NULL){
		packet = calloc(1, sizeof(packet_event_t));
		*packet = *ev;
		packet->sent = 0;
		val.ptr = packet;
		skiplist_insert(que->cache, key, val);

		/*�����͵�packet��ʱ��˳��ѹ��list*/
		list_push(que->l, packet);

		/*�����ֽ�ͳ��*/
		que->total_size += packet->size;

		ret = 0;
	}

	if (que->oldest_ts == -1 || que->oldest_ts > ev->que_ts)
		que->oldest_ts = ev->que_ts;

	return ret;
}

packet_event_t*	pacer_queue_front(pacer_queue_t* que)
{
	packet_event_t* ret = NULL;
	skiplist_iter_t* iter;

	if (skiplist_size(que->cache) == 0)
		return ret;

	/*ȡһ��δ���͵�packet*/
	iter = skiplist_first(que->cache);
	do{
		if (iter != NULL){
			ret = iter->val.ptr;
			if (ret->sent == 0)
				break;
			else
				iter = iter->next[0];
		}
	} while (iter != NULL);

	return ret;
}

void pacer_queue_final(pacer_queue_t* que)
{
	packet_event_t* packet;
	skiplist_item_t key;

	/*ɾ���Ѿ����͵ı���*/
	while (que->l->size > 0){
		packet = list_front(que->l);
		if (packet->sent == 1){
			list_pop(que->l);
			key.u32 = packet->seq;
			skiplist_remove(que->cache, key);
		}
		else
			break;
	}

	/*cache������û�а�������Ϊ��ʼ״̬*/
	if (que->l->size == 0){
		skiplist_clear(que->cache);
		que->total_size = 0;
		que->oldest_ts = -1;
	}
	else{
		packet = list_front(que->l);
		que->oldest_ts = packet->que_ts;
	}
}

/*ɾ����һ����Ԫ*/
void pacer_queue_sent(pacer_queue_t* que, uint32_t seq)
{
	packet_event_t* packet;
	skiplist_iter_t* iter;
	skiplist_item_t key;

	if (skiplist_size(que->cache) == 0)
		return;

	key.u32 = seq;
	iter = skiplist_search(que->cache, key);
	if (iter != NULL){
		packet = iter->val.ptr;
		packet->sent = 1;				/*���Ϊ�Ѿ�����״̬*/

		if (que->total_size >= packet->size)
			que->total_size -= packet->size;
		else
			que->total_size = 0;

		/*ɾ���������Ѿ����͵İ�*/
		pacer_queue_final(que);
	}
}

int	pacer_queue_empty(pacer_queue_t* que)
{
	return skiplist_size(que->cache) == 0 ? 0 : -1;
}

size_t pacer_queue_bytes(pacer_queue_t* que)
{
	return que->total_size;
}

int64_t	pacer_queue_oldest(pacer_queue_t* que)
{
	return que->oldest_ts;
}

uint32_t pacer_queue_target_bitrate_kbps(pacer_queue_t* que, int64_t now_ts)
{
	uint32_t ret = 0, space;

	if (que->oldest_ts != -1 && now_ts > que->oldest_ts){
		space = (uint32_t)(now_ts - que->oldest_ts) / 10;
		if (space > k_max_pace_queue_ms)
			space = 1;
		else
			space = k_max_pace_queue_ms - space;
	}
	else
		space = k_max_pace_queue_ms - 1;

	/*���㻺������500����֮��Ҫ�����������Ĵ���*/
	if (skiplist_size(que->cache) > 0 && que->total_size > 0)
		ret = que->total_size * 8 / space;
	
	return ret;
}

