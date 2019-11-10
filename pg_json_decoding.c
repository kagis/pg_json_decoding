#include "postgres.h"
#include "catalog/namespace.h"
#include "replication/logical.h"
#include "common/base64.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/json.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

extern void _PG_init(void);
extern void _PG_output_plugin_init(OutputPluginCallbacks *cb);

typedef struct _JsonDecodingData {
  MemoryContext context;
  char *pubname;
  Oid pubid;
  bool puballtables;
} JsonDecodingData;

static void pg_decode_startup(
  LogicalDecodingContext *ctx,
  OutputPluginOptions *opt,
  bool is_init
) {
  JsonDecodingData *data;
  ListCell *option;

  data = palloc0(sizeof(JsonDecodingData));
  data->pubname = NULL;
  data->pubid = InvalidOid;
  data->puballtables = false;
  data->context = AllocSetContextCreate(
    ctx->context,
    "pg_json_decoding context",
    ALLOCSET_DEFAULT_MINSIZE,
    ALLOCSET_DEFAULT_INITSIZE,
    ALLOCSET_DEFAULT_MAXSIZE
  );
  ctx->output_plugin_private = data;
  opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;

  if (is_init) {
    return;
  }

  foreach(option, ctx->output_plugin_options) {
    DefElem *elem = lfirst(option);
    Assert(elem->arg == NULL || IsA(elem->arg, String));
    if (strcmp(elem->defname, "publication") == 0) {
      data->pubname = strVal(elem->arg);
    }
  }

  if (data->pubname == NULL) {
    ereport(ERROR, (
      errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("publication parameter missing")
    ));
  }
}

static void pg_decode_shutdown(LogicalDecodingContext *ctx) {
  JsonDecodingData *data = ctx->output_plugin_private;
  MemoryContextDelete(data->context);
}

static void pg_decode_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn) {
  OutputPluginPrepareWrite(ctx, true);
  appendStringInfo(
    ctx->out,
    "{\"kind\":\"begin\",\"committed\":\"%ld\"}",
    txn->commit_time + (POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) * USECS_PER_DAY
  );
  OutputPluginWrite(ctx, true);
}

static void pg_decode_commit_txn(
  LogicalDecodingContext *ctx,
  ReorderBufferTXN *txn,
  XLogRecPtr commit_lsn
) {
  OutputPluginPrepareWrite(ctx, true);
  appendStringInfoString(ctx->out, "{\"kind\":\"commit\"}");
  OutputPluginWrite(ctx, true);
}

static void tuple_to_json(StringInfo out, TupleDesc tupdesc, HeapTuple tuple) {
  Datum tupdat = heap_copy_tuple_as_datum(tuple, tupdesc);
  Datum tupjson = DirectFunctionCall1(row_to_json, tupdat);
  char *tupjsonstr = TextDatumGetCString(tupjson);
  appendStringInfoString(out, tupjsonstr);
}

static void pg_decode_change(
  LogicalDecodingContext *ctx,
  ReorderBufferTXN *txn,
  Relation relation,
  ReorderBufferChange *change
) {
  JsonDecodingData *data;
  Form_pg_class class_form;
  TupleDesc tupdesc;
  MemoryContext old;
  char *table_name;

  data = ctx->output_plugin_private;

  if (data->pubid == InvalidOid) {
    data->pubid = get_publication_oid(data->pubname, false);
    data->puballtables = GetPublication(data->pubid)->alltables;
  }

  if (!data->puballtables && !SearchSysCacheExists2(
    PUBLICATIONRELMAP,
    ObjectIdGetDatum(RelationGetRelid(relation)),
    ObjectIdGetDatum(data->pubid)
  )) {
    return;
  }

  class_form = RelationGetForm(relation);
  tupdesc = RelationGetDescr(relation);
  table_name = NameStr(class_form->relname);

  old = MemoryContextSwitchTo(data->context);
  OutputPluginPrepareWrite(ctx, true);

  appendStringInfoString(ctx->out, "{\"kind\":");
  switch (change->action) {
    case REORDER_BUFFER_CHANGE_INSERT:
      appendStringInfoString(ctx->out, "\"insert\"");
      break;
    case REORDER_BUFFER_CHANGE_UPDATE:
      appendStringInfoString(ctx->out, "\"update\"");
      break;
    case REORDER_BUFFER_CHANGE_DELETE:
      appendStringInfoString(ctx->out, "\"delete\"");
      break;
    default:
      appendStringInfoString(ctx->out, "\"unknown\"");
      break;
  }
  appendStringInfoString(ctx->out, ",\"schema\":");
  escape_json(ctx->out, get_namespace_name(
    get_rel_namespace(RelationGetRelid(relation))
  ));
  appendStringInfoString(ctx->out, ",\"table\":");
  escape_json(ctx->out, table_name);

  if (change->data.tp.oldtuple != NULL) {
    appendStringInfoString(ctx->out, ",\"oldtuple\":");
    tuple_to_json(ctx->out, tupdesc, &change->data.tp.oldtuple->tuple);
  }
  if (change->data.tp.newtuple != NULL) {
    appendStringInfoString(ctx->out, ",\"newtuple\":");
    tuple_to_json(ctx->out, tupdesc, &change->data.tp.newtuple->tuple);
  }
  appendStringInfoChar(ctx->out, '}');

  MemoryContextSwitchTo(old);
  MemoryContextReset(data->context);

  OutputPluginWrite(ctx, true);
}

static void pg_decode_truncate(
  LogicalDecodingContext *ctx,
  ReorderBufferTXN *txn,
  int nrelations,
  Relation relations[],
  ReorderBufferChange *change
) {
  OutputPluginPrepareWrite(ctx, true);
  appendStringInfoString(ctx->out, "{\"kind\":\"truncate\"}");
  OutputPluginWrite(ctx, true);
}

static void pg_decode_message(
  LogicalDecodingContext *ctx,
  ReorderBufferTXN *txn,
  XLogRecPtr lsn,
  bool transactional,
  const char *prefix,
  Size sz,
  const char *message
) {
  JsonDecodingData *data;
  MemoryContext old;
  char *message_b64;

  data = ctx->output_plugin_private;
  old = MemoryContextSwitchTo(data->context);
  OutputPluginPrepareWrite(ctx, true);
  if (transactional) {
    appendStringInfoString(ctx->out, "{\"kind\":\"xmessage\"");
  } else {
    appendStringInfoString(ctx->out, "{\"kind\":\"message\"");
  }
  appendStringInfoString(ctx->out, ",\"prefix\":");
  escape_json(ctx->out, prefix);
  message_b64 = palloc0(pg_b64_enc_len(sz) + 1);
  pg_b64_encode(message, sz, message_b64);
  appendStringInfo(ctx->out, ",\"content\":\"%s\"}", message_b64);
  pfree(message_b64);
  MemoryContextSwitchTo(old);
  MemoryContextReset(data->context);
  OutputPluginWrite(ctx, true);
}

void _PG_output_plugin_init(OutputPluginCallbacks *cb) {
  AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);
  cb->startup_cb = pg_decode_startup;
  cb->begin_cb = pg_decode_begin_txn;
  cb->change_cb = pg_decode_change;
  cb->commit_cb = pg_decode_commit_txn;
  cb->shutdown_cb = pg_decode_shutdown;
  cb->message_cb = pg_decode_message;
  cb->truncate_cb = pg_decode_truncate;
}

void _PG_init(void) {
}
