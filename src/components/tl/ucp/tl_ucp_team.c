/**
 * Copyright (C) Mellanox Technologies Ltd. 2020.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "tl_ucp.h"
#include "tl_ucp_ep.h"
#include "utils/ucc_malloc.h"
enum {
    UCC_TL_UCP_ADDR_EXCHANGE_MAX_ADDRLEN,
    UCC_TL_UCP_ADDR_EXCHANGE_GATHER,
    UCC_TL_UCP_ADDR_EXCHANGE_COMPLETE,
};

ucc_status_t ucc_tl_ucp_addr_exchange_start(ucc_tl_ucp_context_t *ctx,
                                            ucc_team_oob_coll_t oob,
                                            ucc_tl_ucp_addr_storage_t **storage)
{
    ucc_tl_ucp_addr_storage_t *st =
        ucc_malloc(sizeof(*st), "tl_ucp_addr_storage");
    ucc_status_t status;
    if (!st) {
        tl_error(ctx->super.super.lib,
                 "failed to allocate %zd bytes for tl_ucp_addr_storage",
                 sizeof(*st));
        return UCC_ERR_NO_MEMORY;
    }
    if ((NULL == ctx->worker_address) &&
        (UCS_OK != ucp_worker_get_address(ctx->ucp_worker, &ctx->worker_address,
                                          &ctx->ucp_addrlen))) {
        tl_error(ctx->super.super.lib, "failed to get ucp worker address");
        return UCC_ERR_NO_MESSAGE;
    }

    st->ctx       = ctx;
    st->oob       = oob;
    st->state     = UCC_TL_UCP_ADDR_EXCHANGE_MAX_ADDRLEN;
    st->addrlens  = ucc_malloc(sizeof(size_t) * oob.participants, "addrlens");
    st->addresses = NULL;
    if (!st->addrlens) {
        tl_error(ctx->super.super.lib,
                 "failed to allocate %zd bytes for addrlens array",
                 sizeof(size_t) * oob.participants);
        status = UCC_ERR_NO_MEMORY;
        goto err_addrlens;
    }
    status = oob.allgather(&ctx->ucp_addrlen, st->addrlens, sizeof(size_t),
                           oob.coll_info, &st->oob_req);
    if (UCC_OK != status) {
        tl_error(ctx->super.super.lib, "failed to start oob allgather");
        goto err_allgather;
    }
    *storage = st;
    return UCC_OK;

err_allgather:
    free(st->addrlens);
err_addrlens:
    free(st);
    return status;
}

ucc_status_t ucc_tl_ucp_addr_exchange_test(ucc_tl_ucp_addr_storage_t *storage)
{
    ucc_status_t         status;
    void                *my_addr;
    int                  i;
    ucc_team_oob_coll_t *oob = &storage->oob;
    if (storage->state == UCC_TL_UCP_ADDR_EXCHANGE_COMPLETE) {
        return UCC_OK;
    }
    status = oob->req_test(storage->oob_req);
    if (UCC_INPROGRESS == status) {
        return status;
    } else if (UCC_OK != status) {
        tl_error(storage->ctx->super.super.lib, "failed during oob req test");
        goto err;
    }
    oob->req_free(storage->oob_req);

    switch (storage->state) {
    case UCC_TL_UCP_ADDR_EXCHANGE_MAX_ADDRLEN:
        storage->max_addrlen = 0;
        for (i = 0; i < oob->participants; i++) {
            if (storage->addrlens[i] > storage->max_addrlen) {
                storage->max_addrlen = storage->addrlens[i];
            }
        }
        ucc_free(storage->addrlens);
        storage->addrlens = NULL;
        storage->addresses =
            ucc_malloc(storage->max_addrlen * (oob->participants + 1),
                       "tl_ucp_storage_addresses");
        if (!storage->addresses) {
            status = UCC_ERR_NO_MEMORY;
            tl_error(
                storage->ctx->super.super.lib,
                "failed to allocate %zd bytes for tl_ucp storage addresses",
                storage->max_addrlen * (oob->participants + 1));
            goto err;
        }
        my_addr = (void *)((ptrdiff_t)storage->addresses +
                           oob->participants * storage->max_addrlen);
        memcpy(my_addr, storage->ctx->worker_address,
               storage->ctx->ucp_addrlen);
        status =
            oob->allgather(my_addr, storage->addresses, storage->max_addrlen,
                           oob->coll_info, &storage->oob_req);
        if (UCC_OK != status) {
            tl_error(storage->ctx->super.super.lib,
                     "failed to start oob allgather");
            goto err;
        }
        storage->state = UCC_TL_UCP_ADDR_EXCHANGE_GATHER;
        return UCC_INPROGRESS;
    case UCC_TL_UCP_ADDR_EXCHANGE_GATHER:
        storage->state = UCC_TL_UCP_ADDR_EXCHANGE_COMPLETE;
        break;
    }
    return UCC_OK;

err:
    free(storage->addrlens);
    free(storage->addresses);
    free(storage);
    return status;
}

void ucc_tl_ucp_addr_storage_free(ucc_tl_ucp_addr_storage_t *storage)
{
    free(storage->addresses);
    ucc_assert(NULL == storage->addrlens);
    free(storage);
}

UCC_CLASS_INIT_FUNC(ucc_tl_ucp_team_t, ucc_base_context_t *tl_context,
                    const ucc_base_team_params_t *params)
{
    ucc_status_t          status = UCC_OK;
    ucc_tl_ucp_context_t *ctx =
        ucc_derived_of(tl_context, ucc_tl_ucp_context_t);
    UCC_CLASS_CALL_SUPER_INIT(ucc_tl_team_t, &ctx->super);
    /* TODO: init based on ctx settings and on params: need to check
             if all the necessary ranks mappings are provided */
    self->context_ep_storage = 0;
    self->addr_storage       = NULL;
    self->size               = params->params.oob.participants;
    if (self->context_ep_storage) {
        self->status = UCC_OK;
    } else {
        self->status = UCC_INPROGRESS;
        status       = ucc_tl_ucp_addr_exchange_start(ctx, params->params.oob,
                                                &self->addr_storage);
    }
    tl_info(tl_context->lib, "posted tl team: %p", self);
    return status;
}

UCC_CLASS_CLEANUP_FUNC(ucc_tl_ucp_team_t)
{
    ucc_tl_ucp_context_t *ctx = UCC_TL_UCP_TEAM_CTX(self);
    if (self->addr_storage) {
        ucc_tl_ucp_addr_storage_free(self->addr_storage);
    }
    if (self->eps) {
        if (UCC_OK != ucc_tl_ucp_close_eps(ctx, self->eps, self->size)) {
            tl_error(self->super.super.context->lib,
                     "failed to close team eps");
        }
    }
    tl_info(self->super.super.context->lib, "finalizing tl team: %p", self);
}

static ucc_status_t ucc_tl_ucp_team_preconnect(ucc_tl_ucp_team_t *team)
{
    ucc_tl_ucp_context_t *ctx = UCC_TL_UCP_TEAM_CTX(team);
    int                   i;
    ucc_status_t          status;
    for (i = 0; i < team->size; i++) {
        status = ucc_tl_ucp_connect_ep(ctx, team, team->addr_storage->addresses,
                                       team->addr_storage->max_addrlen, i);
        if (UCC_OK != status) {
            return status;
        }
    }
    tl_debug(UCC_TL_TEAM_LIB(team), "preconnected tl team: %p, num_eps %d",
             team, team->size);
    return UCC_OK;
}

ucc_status_t ucc_tl_ucp_team_create_test(ucc_base_team_t *tl_team)
{
    ucc_tl_ucp_team_t    *team = ucc_derived_of(tl_team, ucc_tl_ucp_team_t);
    ucc_tl_ucp_context_t *ctx  = UCC_TL_UCP_TEAM_CTX(team);
    ucc_status_t          status;
    if (team->status == UCC_OK) {
        return UCC_OK;
    }
    if (team->addr_storage) {
        status = ucc_tl_ucp_addr_exchange_test(team->addr_storage);
        if (UCC_INPROGRESS == status) {
            return UCC_INPROGRESS;
        } else if (UCC_OK != status) {
            return status;
        }
        team->eps = ucc_calloc(sizeof(ucp_ep_h), team->size, "team_eps");
        if (!team->eps) {
            tl_error(tl_team->context->lib,
                     "failed to allocate %zd bytes for team eps",
                     sizeof(ucp_ep_h) * team->size);
            return UCC_ERR_NO_MEMORY;
        }
        if (ctx->preconnect) {
            status = ucc_tl_ucp_team_preconnect(team);
            if (UCC_OK != status) {
                goto err_preconnect;
            }
        }
    }
    tl_info(tl_team->context->lib, "initialized tl team: %p", team);
    team->status = UCC_OK;
    return UCC_OK;

err_preconnect:
    ucc_free(team->eps);
    return status;
}

UCC_CLASS_DEFINE(ucc_tl_ucp_team_t, ucc_tl_team_t);
