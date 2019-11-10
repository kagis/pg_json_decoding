create table foo(a text, b text);
alter table foo replica identity full;
create publication test for table foo;
-- create publication test for all tables;

select pg_create_logical_replication_slot('test', 'pg_json_decoding');
insert into foo values ('1', 'a');
update foo set a = '2';
delete from foo;
select pg_logical_emit_message(true, 'message', 'hello');

select *
from pg_logical_slot_get_changes('test', null, null, 'publication', 'test');
