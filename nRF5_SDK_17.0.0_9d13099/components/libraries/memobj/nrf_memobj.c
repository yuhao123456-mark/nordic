/**
 * Copyright (c) 2017 - 2020, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "nrf_memobj.h"
#include "nrf_atomic.h"
#include "nrf_assert.h"
#include "nrf_log.h"

typedef struct memobj_elem_s memobj_elem_t;

/** @brief Standard chunk header. */
typedef struct
{
    memobj_elem_t * p_next; ///< Pointer to the next element.
} memobj_header_t;

/** @brief Head header extension fields. */
typedef struct
{
    uint8_t  user_cnt;    ///< User counter (see @ref nrf_memobj_get and @ref nrf_memobj_put).
    uint8_t  chunk_cnt;   ///< Number of chunks in the object.
    uint16_t chunk_size;  ///< Single chunk size
} memobj_head_header_fields_t;

/** @brief Head header extension. */
typedef struct
{
    union
    {
        nrf_atomic_u32_t            atomic_user_cnt;
        memobj_head_header_fields_t fields;
    } data;
} memobj_head_header_t;

/** @brief Head chunk structure. */
typedef struct
{
    memobj_header_t      header;      ///< Standard header.
    memobj_head_header_t head_header; ///< Head-specific header part.
    uint8_t              data[1];     ///< Data.
} memobj_head_t;

STATIC_ASSERT(sizeof(memobj_header_t) == NRF_MEMOBJ_STD_HEADER_SIZE);

/** @brief Standard chunk structure. */
struct memobj_elem_s
{
    memobj_header_t  header;  ///< Standard header.
    uint8_t          data[1]; ///< Data.
};

static void * nrf_balloc_idx2block(nrf_balloc_t const * p_pool, uint8_t idx)
{
    ASSERT(p_pool != NULL);
    return (uint8_t *)(p_pool->p_memory_begin) + ((size_t)(idx) * p_pool->block_size);
}

/*void * nrf_balloc_alloc(nrf_balloc_t const * p_pool)
{
    ASSERT(p_pool != NULL);

    void * p_block = NULL;

    CRITICAL_REGION_ENTER();

    if (p_pool->p_cb->p_stack_pointer > p_pool->p_stack_base)
    {
        // Allocate block.
        p_block = nrf_balloc_idx2block(p_pool, *--(p_pool->p_cb->p_stack_pointer));

        // Update utilization statistics.
        uint8_t utilization = p_pool->p_stack_limit - p_pool->p_cb->p_stack_pointer;
        if (p_pool->p_cb->max_utilization < utilization)
        {
            p_pool->p_cb->max_utilization = utilization;
        }
    }

    CRITICAL_REGION_EXIT();

#if NRF_BALLOC_CONFIG_DEBUG_ENABLED
    if (p_block != NULL)
    {
        p_block = nrf_balloc_block_unwrap(p_pool, p_block);
    }
#endif

    NRF_LOG_INST_DEBUG(p_pool->p_log, "Allocating element: 0x%08X", p_block);

    return p_block;
}*/

ret_code_t nrf_memobj_pool_init(nrf_memobj_pool_t const * p_pool)
{
    return nrf_balloc_init((nrf_balloc_t const *)p_pool);
}

nrf_memobj_t * nrf_memobj_alloc(nrf_memobj_pool_t const * p_pool,
                                size_t size)
{
    uint32_t bsize = (uint32_t)NRF_BALLOC_ELEMENT_SIZE((nrf_balloc_t const *)p_pool) - sizeof(memobj_header_t);
    uint8_t num_of_chunks = (uint8_t)CEIL_DIV(size + sizeof(memobj_head_header_t), bsize);

    memobj_head_t * p_head = nrf_balloc_alloc((nrf_balloc_t const *)p_pool);
    if (p_head == NULL)
    {
        return NULL;
    }
    p_head->head_header.data.fields.user_cnt = 0;
    p_head->head_header.data.fields.chunk_cnt = 1;
    p_head->head_header.data.fields.chunk_size = bsize;

    memobj_header_t * p_prev = (memobj_header_t *)p_head;
    memobj_header_t * p_curr;
    uint32_t i;
    uint32_t chunk_less1 = (uint32_t)num_of_chunks - 1;

    p_prev->p_next =  (memobj_elem_t *)p_pool;
    for (i = 0; i < chunk_less1; i++)
    {
        p_curr = (memobj_header_t *)nrf_balloc_alloc((nrf_balloc_t const *)p_pool);
        if (p_curr)
        {
            (p_head->head_header.data.fields.chunk_cnt)++;
            p_prev->p_next = (memobj_elem_t *)p_curr;
            p_curr->p_next = (memobj_elem_t *)p_pool;
            p_prev = p_curr;
        }
        else
        {
            //Could not allocate all requested buffers
            nrf_memobj_free((nrf_memobj_t *)p_head);
            return NULL;
        }
    }
    return (nrf_memobj_t *)p_head;
}

void nrf_memobj_free(nrf_memobj_t * p_obj)
{
    memobj_head_t * p_head = (memobj_head_t *)p_obj;
    uint8_t chunk_cnt = p_head->head_header.data.fields.chunk_cnt;
    uint32_t i;
    memobj_header_t * p_curr = (memobj_header_t *)p_obj;
    memobj_header_t * p_next;
    uint32_t chunk_less1 = (uint32_t)chunk_cnt - 1;

    for (i = 0; i < chunk_less1; i++)
    {
        p_curr = (memobj_header_t *)p_curr->p_next;
    }
    nrf_balloc_t const * p_pool2 = (nrf_balloc_t const *)p_curr->p_next;

    p_curr = (memobj_header_t *)p_obj;
    for (i = 0; i < chunk_cnt; i++)
    {
        p_next = (memobj_header_t *)p_curr->p_next;
        nrf_balloc_free(p_pool2, p_curr);
        p_curr = p_next;
    }
}

void nrf_memobj_get(nrf_memobj_t const * p_obj)
{
    memobj_head_t * p_head = (memobj_head_t *)p_obj;
    (void)nrf_atomic_u32_add(&p_head->head_header.data.atomic_user_cnt, 1);
}

void nrf_memobj_put(nrf_memobj_t * p_obj)
{
    memobj_head_t * p_head = (memobj_head_t *)p_obj;
    uint32_t user_cnt = nrf_atomic_u32_sub(&p_head->head_header.data.atomic_user_cnt, 1);
    memobj_head_header_fields_t * p_fields = (memobj_head_header_fields_t *)&user_cnt;
    if (p_fields->user_cnt == 0)
    {
        nrf_memobj_free(p_obj);
    }
}

static void memobj_op(nrf_memobj_t * p_obj,
                      void *         p_data,
                      size_t *       p_len,
                      size_t         offset,
                      bool read)
{

    ASSERT(p_obj);

    memobj_head_t * p_head       = (memobj_head_t *)p_obj;
    memobj_elem_t * p_curr_chunk = (memobj_elem_t *)p_obj;
    size_t          obj_capacity;
    size_t          chunk_size;
    size_t          chunk_idx;
    size_t          chunk_offset;
    size_t          len;

    obj_capacity = (p_head->head_header.data.fields.chunk_size *
                    p_head->head_header.data.fields.chunk_cnt) -
                    sizeof(memobj_head_header_fields_t);

    ASSERT(offset < obj_capacity);

    chunk_size   = p_head->head_header.data.fields.chunk_size;
    chunk_idx    = (offset + sizeof(memobj_head_header_fields_t)) / chunk_size;
    chunk_offset = (offset + sizeof(memobj_head_header_fields_t)) % chunk_size;
    len          = ((*p_len + offset) > obj_capacity) ? obj_capacity - offset : *p_len;

    //Return number of available bytes
    *p_len = len;

    //Move to the first chunk to be used
    while (chunk_idx > 0)
    {
        p_curr_chunk = p_curr_chunk->header.p_next;
        chunk_idx--;
    }

    size_t user_mem_offset  = 0;
    size_t curr_cpy_size    = chunk_size - chunk_offset;
    curr_cpy_size = curr_cpy_size > len ? len : curr_cpy_size;

    while (len)
    {
        void * p_user_mem = &((uint8_t *)p_data)[user_mem_offset];
        void * p_obj_mem  = &p_curr_chunk->data[chunk_offset];
        if (read)
        {
            memcpy(p_user_mem, p_obj_mem, curr_cpy_size);
        }
        else
        {
            memcpy(p_obj_mem, p_user_mem, curr_cpy_size);
        }

        chunk_offset     = 0;
        p_curr_chunk     = p_curr_chunk->header.p_next;
        len             -= curr_cpy_size;
        user_mem_offset += curr_cpy_size;
        curr_cpy_size    = (chunk_size > len) ? len : chunk_size;
    }
}

void nrf_memobj_write(nrf_memobj_t * p_obj,
                      void *         p_data,
                      size_t         len,
                      size_t         offset)
{

    size_t op_len = len;
    memobj_op(p_obj, p_data, &op_len, offset, false);
    ASSERT(op_len == len);
}

void nrf_memobj_read(nrf_memobj_t * p_obj,
                     void *         p_data,
                     size_t         len,
                     size_t         offset)
{
    size_t op_len = len;
    memobj_op(p_obj, p_data, &op_len, offset, true);
    ASSERT(op_len == len);

}

static uint8_t nrf_balloc_block2idx(nrf_balloc_t const * p_pool, void const * p_block)
{
    ASSERT(p_pool != NULL);
    return ((size_t)(p_block) - (size_t)(p_pool->p_memory_begin)) / p_pool->block_size;
}

/*void nrf_balloc_free(nrf_balloc_t const * p_pool, void * p_element)
{
    ASSERT(p_pool != NULL);
    ASSERT(p_element != NULL)

    NRF_LOG_INST_DEBUG(p_pool->p_log, "Freeing element: 0x%08X", p_element);

#if NRF_BALLOC_CONFIG_DEBUG_ENABLED
    void * p_block = nrf_balloc_element_wrap(p_pool, p_element);

    // These checks could be done outside critical region as they use only pool configuration data.
    if (NRF_BALLOC_DEBUG_BASIC_CHECKS_GET(p_pool->debug_flags))
    {
        uint8_t pool_size  = p_pool->p_stack_limit - p_pool->p_stack_base;
        void *p_memory_end = (uint8_t *)(p_pool->p_memory_begin) + (pool_size * p_pool->block_size);

        // Check if the element belongs to this pool.
        if ((p_block < p_pool->p_memory_begin) || (p_block >= p_memory_end))
        {
            NRF_LOG_INST_ERROR(p_pool->p_log,
                              "Attempted to free element (0x%08X) that does not belong to the pool.",
                              p_element);
            APP_ERROR_CHECK_BOOL(false);
        }

        // Check if the pointer is valid.
        if ((((size_t)(p_block) - (size_t)(p_pool->p_memory_begin)) % p_pool->block_size) != 0)
        {
            NRF_LOG_INST_ERROR(p_pool->p_log,
                               "Attempted to free corrupted element address (0x%08X).", p_element);
            APP_ERROR_CHECK_BOOL(false);
        }
    }
#else
    void * p_block = p_element;
#endif // NRF_BALLOC_CONFIG_DEBUG_ENABLED

    CRITICAL_REGION_ENTER();

#if NRF_BALLOC_CONFIG_DEBUG_ENABLED
    // These checks have to be done in critical region as they use p_pool->p_stack_pointer.
    if (NRF_BALLOC_DEBUG_BASIC_CHECKS_GET(p_pool->debug_flags))
    {
        // Check for allocated/free ballance.
        if (p_pool->p_cb->p_stack_pointer >= p_pool->p_stack_limit)
        {
            NRF_LOG_INST_ERROR(p_pool->p_log,
                               "Attempted to free an element (0x%08X) while the pool is full.",
                               p_element);
            APP_ERROR_CHECK_BOOL(false);
        }
    }

    if (NRF_BALLOC_DEBUG_DOUBLE_FREE_CHECK_GET(p_pool->debug_flags))
    {
        // Check for double free.
        for (uint8_t * p_idx = p_pool->p_stack_base; p_idx < p_pool->p_cb->p_stack_pointer; p_idx++)
        {
            if (nrf_balloc_idx2block(p_pool, *p_idx) == p_block)
            {
                NRF_LOG_INST_ERROR(p_pool->p_log, "Attempted to double-free an element (0x%08X).",
                                   p_element);
                APP_ERROR_CHECK_BOOL(false);
            }
        }
    }
#endif // NRF_BALLOC_CONFIG_DEBUG_ENABLED

    // Free the element.
    *(p_pool->p_cb->p_stack_pointer)++ = nrf_balloc_block2idx(p_pool, p_block);

    CRITICAL_REGION_EXIT();
}*/