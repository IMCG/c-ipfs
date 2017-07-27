#include <stdlib.h>
#include "libp2p/conn/session.h"
#include "libp2p/utils/vector.h"
#include "ipfs/exchange/bitswap/wantlist_queue.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"

/**
 * Implementation of the WantlistQueue
 */

/**
 * remove this session from the lists of sessions that are looking for this WantListQueueEntry
 * @param entry the entry
 * @param session who was looking for it
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_bitswap_wantlist_queue_entry_decrement(struct WantListQueueEntry* entry, const struct WantListSession* session) {
	for(size_t i = 0; i < entry->sessionsRequesting->total; i++) {
		const struct WantListSession* current = (const struct WantListSession*)libp2p_utils_vector_get(entry->sessionsRequesting, i);
		if (ipfs_bitswap_wantlist_session_compare(session, current) == 0) {
			libp2p_utils_vector_delete(entry->sessionsRequesting, i);
			return 1;
		}
	}
	return 0;
}


/***
 * Initialize a new Wantlist (there should only be 1 per instance)
 * @returns a new WantList
 */
struct WantListQueue* ipfs_bitswap_wantlist_queue_new() {
	struct WantListQueue* wantlist = (struct WantListQueue*) malloc(sizeof(struct WantListQueue));
	if (wantlist != NULL) {
		pthread_mutex_init(&wantlist->wantlist_mutex, NULL);
		wantlist->queue = NULL;
	}
	return wantlist;
}

/***
 * Deallocate resources of a WantList
 * @param wantlist the WantList
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_queue_free(struct WantListQueue* wantlist) {
	if (wantlist != NULL) {
		if (wantlist->queue != NULL) {
			libp2p_utils_vector_free(wantlist->queue);
			wantlist->queue = NULL;
		}
		free(wantlist);
	}
	return 1;
}

/***
 * Add a Cid to the WantList
 * @param wantlist the WantList to add to
 * @param cid the Cid to add
 * @returns the correct WantListEntry or NULL if error
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_add(struct WantListQueue* wantlist, const struct Cid* cid, const struct WantListSession* session) {
	struct WantListQueueEntry* entry = NULL;
	if (wantlist != NULL) {
		pthread_mutex_lock(&wantlist->wantlist_mutex);
		if (wantlist->queue == NULL) {
			wantlist->queue = libp2p_utils_vector_new(1);
		}
		entry = ipfs_bitswap_wantlist_queue_find(wantlist, cid);
		if (entry == NULL) {
			// create a new one
			entry = ipfs_bitswap_wantlist_queue_entry_new();
			entry->cid = ipfs_cid_copy(cid);
			entry->priority = 1;
			libp2p_utils_vector_add(wantlist->queue, entry);
		}
		libp2p_utils_vector_add(entry->sessionsRequesting, session);
		pthread_mutex_unlock(&wantlist->wantlist_mutex);
	}
	return entry;
}

/***
 * Remove (decrement the counter) a Cid from the WantList
 * @param wantlist the WantList
 * @param cid the Cid
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_wantlist_queue_remove(struct WantListQueue* wantlist, const struct Cid* cid, const struct WantListSession* session) {
	//TODO: remove if counter is <= 0
	if (wantlist != NULL) {
		struct WantListQueueEntry* entry = ipfs_bitswap_wantlist_queue_find(wantlist, cid);
		if (entry != NULL) {
			ipfs_bitswap_wantlist_queue_entry_decrement(entry, session);
			return 1;
		}
	}
	return 0;
}

/***
 * Find a Cid in the WantList
 * @param wantlist the list
 * @param cid the Cid
 * @returns the WantListQueueEntry
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_find(struct WantListQueue* wantlist, const struct Cid* cid) {
	for (size_t i = 0; i < wantlist->queue->total; i++) {
		struct WantListQueueEntry* entry = (struct WantListQueueEntry*) libp2p_utils_vector_get(wantlist->queue, i);
		if (entry == NULL) {
			//TODO: something went wrong. This should be logged.
			return NULL;
		}
		if (ipfs_cid_compare(cid, entry->cid) == 0) {
			return entry;
		}
	}
	return NULL;
}

/***
 * Initialize a WantListQueueEntry
 * @returns a new WantListQueueEntry
 */
struct WantListQueueEntry* ipfs_bitswap_wantlist_queue_entry_new() {
	struct WantListQueueEntry* entry = (struct WantListQueueEntry*) malloc(sizeof(struct WantListQueueEntry));
	if (entry != NULL) {
		entry->block = NULL;
		entry->cid = NULL;
		entry->priority = 0;
		entry->sessionsRequesting = NULL;
		entry->attempts = 0;
	}
	return entry;
}

/***
 * Free a WantListQueueENtry struct
 * @param entry the struct
 * @returns true(1)
 */
int ipfs_bitswap_wantlist_queue_entry_free(struct WantListQueueEntry* entry) {
	if (entry != NULL) {
		if (entry->block != NULL) {
			ipfs_block_free(entry->block);
			entry->block = NULL;
		}
		if (entry->cid != NULL) {
			ipfs_cid_free(entry->cid);
			entry->cid = NULL;
		}
		if (entry->sessionsRequesting != NULL) {
			libp2p_utils_vector_free(entry->sessionsRequesting);
			entry->sessionsRequesting = NULL;
		}
		free(entry);
	}
	return 1;
}

int ipfs_bitswap_wantlist_session_compare(const struct WantListSession* a, const struct WantListSession* b) {
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL && b != NULL)
		return -1;
	if (a != NULL && b == NULL)
		return 1;
	if (a->type != b->type)
		return b->type - a->type;
	if (a->type == WANTLIST_SESSION_TYPE_LOCAL) {
		// it's local, there should be only 1
		return 0;
	} else {
		struct SessionContext* contextA = (struct SessionContext*)a->context;
		struct SessionContext* contextB = (struct SessionContext*)b->context;
		return libp2p_session_context_compare(contextA, contextB);
	}
}

/**
 * determine if any of the sessions are referring to the local node
 * @param sessions a vector of WantlistSession
 * @returns true(1) if any of the sessions are local, false otherwise
 */
int ipfs_bitswap_wantlist_local_request(struct Libp2pVector* sessions)
{
	for(int i = 0; i < sessions->total; i++) {
		struct WantListSession* curr = (struct WantListSession*) libp2p_utils_vector_get(sessions, i);
		if (curr != NULL && curr->type == WANTLIST_SESSION_TYPE_LOCAL)
			return 1;
	}
	return 0;
}

/***
 * Attempt to retrieve a block from the local blockstore
 *
 * @param context the BitswapContext
 * @param cid the id to look for
 * @param block where to put the results
 * @returns true(1) if found, false(0) otherwise
 */
int ipfs_bitswap_wantlist_get_block_locally(struct BitswapContext* context, struct Cid* cid, struct Block** block) {
	return context->ipfsNode->blockstore->Get(context->ipfsNode->blockstore->blockstoreContext, cid, block);
}

/***
 * Retrieve a block. The only information we have is the cid
 *
 * This will ask the network for who has the file, using the router.
 * It will then ask the specific nodes for the file. This method
 * does not queue anything. It actually does the work.
 *
 * @param context the BitswapContext
 * @param cid the id of the file
 * @param block where to put the results
 * @returns true(1) if found, false(0) otherwise
 */
int ipfs_bitswap_wantlist_get_block_remote(struct BitswapContext* context, struct Cid* cid, struct Block** block) {
	//TODO: Implement this workhorse of a method
	return 0;
}

/**
 * Called by the Bitswap engine, this processes an item on the WantListQueue
 * @param context the context
 * @param entry the WantListQueueEntry
 * @returns true(1) on success, false(0) if not.
 */
int ipfs_bitswap_wantlist_process_entry(struct BitswapContext* context, struct WantListQueueEntry* entry) {
	int local_request = ipfs_bitswap_wantlist_local_request(entry->sessionsRequesting);
	int have_local = ipfs_bitswap_wantlist_get_block_locally(context, entry->cid, &entry->block);
	// should we go get it?
	if (!local_request && !have_local) {
		return 0;
	}
	if (local_request && !have_local) {
		if (!ipfs_bitswap_wantlist_get_block_remote(context, entry->cid, &entry->block)) {
			// if we were unsuccessful in retrieving it, put it back in the queue?
			// I don't think so. But I'm keeping this counter here until we have
			// a final decision. Maybe lower the priority?
			entry->attempts++;
			return 0;
		}
	}
	if (entry->block != NULL) {
		// okay we have the block.
		// fulfill the requests
		for(size_t i = 0; i < entry->sessionsRequesting->total; i++) {
			// TODO: Review this code.
			struct WantListSession* session = (struct WantListSession*) libp2p_utils_vector_get(entry->sessionsRequesting, i);
			if (session->type == WANTLIST_SESSION_TYPE_LOCAL) {
				context->ipfsNode->exchange->HasBlock(context->ipfsNode->exchange, entry->block);
			} else {
				struct SessionContext* sessionContext = (struct SessionContext*) session->context;
				ipfs_bitswap_peer_request_queue_fill(context->peerRequestQueue, sessionContext, entry->block);
			}
		}

	}
	return 0;
}
