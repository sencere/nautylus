#include "nautylus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void ng_test_fail_after(size_t count);
extern void ng_test_fail_reset(void);
extern size_t ng_test_encode_value(const ng_value *, char *, size_t);
extern ng_status ng_test_decode_value(const char *, ng_value *, void **);
enum ng_test_import_stage { NG_TEST_IMPORT_NONE=0, NG_TEST_IMPORT_SNAPSHOT, NG_TEST_IMPORT_PARSE_NODES, NG_TEST_IMPORT_SYMBOL, NG_TEST_IMPORT_NODE, NG_TEST_IMPORT_LABEL, NG_TEST_IMPORT_NODE_PROPERTY, NG_TEST_IMPORT_PARSE_RELATIONSHIPS, NG_TEST_IMPORT_RELATIONSHIP, NG_TEST_IMPORT_RELATIONSHIP_PROPERTY, NG_TEST_IMPORT_ADJACENCY, NG_TEST_IMPORT_COMPLETE };
extern enum ng_test_import_stage ng_test_import_current_stage(void);
extern uint64_t ng_test_import_stage_mask(void);
#ifndef NAUTYLUS_CLI
#define NAUTYLUS_CLI "./nautylus"
#endif

static int edge_count(const ng_relationship *r, void *ctx) { (void)r; (*(size_t *)ctx)++; return 1; }
static int node_count_cb(ng_node_id n, uint32_t d, void *ctx) { (void)n; (void)d; (*(size_t *)ctx)++; return 1; }
static void write_import_files(void) {
    FILE *n=fopen("nodes.tsv","wb"), *r=fopen("rels.tsv","wb"); assert(n&&r);
    fputs("node\tx\tP,Q,P\ta=s:6f6e65\nnode\ty\tP\tb=s:74776f\n",n);
    fputs("relationship\tr\tx\tKNOWS\ty\tc=s:7468726565\n",r); fclose(n); fclose(r);
}
static void remove_import_files(void) { remove("nodes.tsv"); remove("rels.tsv"); }
static int same_file(const char*a,const char*b){FILE*x=fopen(a,"rb"),*y=fopen(b,"rb");int ca,cb;if(!x||!y){if(x)fclose(x);if(y)fclose(y);return 0;}do{ca=fgetc(x);cb=fgetc(y);if(ca!=cb){fclose(x);fclose(y);return 0;}}while(ca!=EOF&&cb!=EOF);fclose(x);fclose(y);return 1;}
static int match_count_cb(ng_node_id n, void *ctx) { (void)n; (*(size_t *)ctx)++; return 1; }
static void fixture(ng_graph **out, ng_node_id *old) {
    ng_symbol_id p,k; ng_value v; assert(ng_create(out,"test.ng")==NG_OK);
    assert(ng_symbol(*out,"Existing",&p)==NG_OK); assert(ng_symbol(*out,"name",&k)==NG_OK);
    assert(ng_node_create(*out,&p,1,old)==NG_OK); v.type=NG_VALUE_STRING;v.length=4;v.as.string="keep";assert(ng_node_set(*out,*old,k,&v)==NG_OK);
    assert(ng_validate(*out)==NG_OK);
}
static void codec_tests(void){char b[256];ng_value v,o;void*owned;size_t n;int64_t ints[]={(int64_t)INT64_MIN,(int64_t)INT64_MAX,-1,0,1};size_t i;for(i=0;i<sizeof(ints)/sizeof(ints[0]);i++){memset(&v,0,sizeof(v));v.type=NG_VALUE_INT64;v.as.integer=ints[i];n=ng_test_encode_value(&v,b,sizeof(b));assert(n&&ng_test_decode_value(b,&o,&owned)==NG_OK&&o.type==v.type&&o.as.integer==v.as.integer&&!owned);}for(i=0;i<2;i++){uint64_t bits=i?0x8000000000000000ULL:0;memset(&v,0,sizeof(v));v.type=NG_VALUE_DOUBLE;memcpy(&v.as.real,&bits,8);n=ng_test_encode_value(&v,b,sizeof(b));assert(n&&ng_test_decode_value(b,&o,&owned)==NG_OK);memcpy(&bits,&o.as.real,8);assert(bits==(i?0x8000000000000000ULL:0)&&!owned);}memset(&v,0,sizeof(v));v.type=NG_VALUE_STRING;v.length=7;v.as.string="a\0\t\r\n\\x";n=ng_test_encode_value(&v,b,sizeof(b));assert(n&&ng_test_decode_value(b,&o,&owned)==NG_OK&&o.length==v.length&&!memcmp(o.as.string,v.as.string,v.length));free(owned);memset(&v,0,sizeof(v));v.type=NG_VALUE_BYTES;v.length=4;v.as.bytes=(const unsigned char*)"\0\t\r\n";n=ng_test_encode_value(&v,b,sizeof(b));assert(n&&ng_test_decode_value(b,&o,&owned)==NG_OK&&o.length==v.length&&!memcmp(o.as.bytes,v.as.bytes,v.length));free(owned);{const char*bad[]={"","n:","n0","b:","b:2","b:true","i:","i:-","i:01","i:-0","i:+1","i: 1","i:1 ","i:9223372036854775808","i:-9223372036854775809","d:","d:0","d:000000000000000g","d:00000000000000000","s:0","s:zz","x:0","x:gg","q:anything"};for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++){memset(&o,0,sizeof(o));owned=(void*)1;assert(ng_test_decode_value(bad[i],&o,&owned)==NG_PARSE_ERROR&&owned==NULL);}}}
int main(void) {
    ng_graph *g,*r; ng_symbol_id p,w,k; ng_node_id a,b; ng_relationship_id e; ng_value v; size_t n; codec_tests();
    remove("test.ng"); assert(ng_create(&g,"test.ng")==NG_OK); assert(ng_symbol(g,"Person",&p)==NG_OK); assert(ng_symbol(g,"WORKS_AT",&w)==NG_OK); assert(ng_symbol(g,"name",&k)==NG_OK); assert(ng_node_create(g,&p,1,&a)==NG_OK); assert(ng_node_create(g,0,0,&b)==NG_OK); v.type=NG_VALUE_STRING;v.length=5;v.as.string="Alice";assert(ng_node_set(g,a,k,&v)==NG_OK);assert(ng_relationship_create(g,a,w,b,&e)==NG_OK);assert(ng_relationship_create(g,b,w,a,&e)==NG_OK);assert(ng_save(g)==NG_OK);ng_close(g);assert(ng_open(&r,"test.ng")==NG_OK);assert(ng_validate(r)==NG_OK);n=0;assert(ng_node_relationships(r,a,NG_DIRECTION_OUTGOING,w,edge_count,&n)==NG_OK&&n==1);n=0;assert(ng_traverse(r,a,0,node_count_cb,&n)==NG_OK&&n==2);n=0;assert(ng_query_nodes(r,"MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n:Person) WHERE id(n) = 1 RETURN n",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n:Person) WHERE n.id = 1 RETURN n",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n) RETURN n LIMIT 1",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n:Person)-[:WORKS_AT]->(m) RETURN m",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n:Person)-[:WORKS_AT*1..2]->(m:Person) RETURN m",match_count_cb,&n)==NG_OK&&n==1);n=0;assert(ng_query_nodes(r,"MATCH (n)-[:WORKS_AT]->(m:Person) WHERE m.name = \"Alice\" RETURN n",match_count_cb,&n)==NG_OK&&n==1);{FILE*f=fopen("projection.out","wb");assert(f);assert(ng_query_print(r,"MATCH (n:Person) RETURN n.name",f)==NG_OK);assert(fclose(f)==0);f=fopen("projection.expected","wb");assert(f);fputs("Alice\n",f);assert(fclose(f)==0);assert(same_file("projection.out","projection.expected"));remove("projection.out");remove("projection.expected");}{FILE*f=fopen("columns.out","wb");assert(f);assert(ng_query_print(r,"MATCH (n:Person)-[:WORKS_AT]->(m) RETURN n.name, m.id",f)==NG_OK);assert(fclose(f)==0);f=fopen("columns.expected","wb");assert(f);fputs("Alice\t2\n",f);assert(fclose(f)==0);assert(same_file("columns.out","columns.expected"));remove("columns.out");remove("columns.expected");}{FILE*f=fopen("multi.out","wb");assert(f);assert(ng_query_print(r,"MATCH (n:Person)-[:WORKS_AT*2]->(m:Person) RETURN m.name",f)==NG_OK);assert(fclose(f)==0);f=fopen("multi.expected","wb");assert(f);fputs("Alice\n",f);assert(fclose(f)==0);assert(same_file("multi.out","multi.expected"));remove("multi.out");remove("multi.expected");}{char plan[128];assert(ng_query_explain("MATCH (n:Person)-[:WORKS_AT*1..2]->(m) RETURN m LIMIT 1",plan,sizeof(plan))==NG_OK&&strstr(plan,"Expand")!=NULL);}assert(ng_query_nodes(r,"MATCH n RETURN n",match_count_cb,&n)==NG_PARSE_ERROR);assert(ng_query_nodes(r,"MATCH (n) RETURN m",match_count_cb,&n)==NG_PARSE_ERROR);n=0;assert(ng_query_nodes(r,"MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n.name",match_count_cb,&n)==NG_OK&&n==1);assert(ng_query_nodes(r,"MATCH (n)-[:WORKS_AT*1..65]->(m) RETURN m",match_count_cb,&n)==NG_PARSE_ERROR);ng_close(r);remove("test.ng");
    write_import_files();
    { size_t fail; uint64_t mask=0; int saw_failure=0,saw_success=0;
      for(fail=0;fail<10000;fail++) { ng_import_diagnostic d; size_t accepted=999; ng_status st; ng_node_id old; uint64_t attempt_mask;
        fixture(&g,&old); d.line=d.column=999; d.status=NG_INVALID_ARGUMENT; ng_test_fail_after(fail);
        st=ng_import_property_graph(g,"nodes.tsv","rels.tsv",0,&accepted,&d); attempt_mask=ng_test_import_stage_mask(); mask|=attempt_mask; ng_test_fail_reset();
        if(st==NG_OK) { assert(accepted==3); assert(ng_test_import_current_stage()==NG_TEST_IMPORT_COMPLETE); assert(ng_validate(g)==NG_OK); saw_success=1; ng_close(g); break; }
        assert(st==NG_OOM&&accepted==0&&d.status==NG_OOM&&d.line==0&&d.column==0); saw_failure=1; assert(ng_node_count(g)==1&&ng_relationship_count(g)==0&&ng_validate(g)==NG_OK); assert(ng_node_get(g,old,&(ng_node){0})==NG_OK);
        accepted=999; memset(&d,0,sizeof d); assert(ng_import_property_graph(g,"nodes.tsv","rels.tsv",0,&accepted,&d)==NG_OK); assert(ng_validate(g)==NG_OK); ng_close(g);
      }
      assert(saw_failure&&saw_success); assert(mask&(1u<<NG_TEST_IMPORT_SNAPSHOT)); assert(mask&(1u<<NG_TEST_IMPORT_PARSE_NODES)); assert(mask&(1u<<NG_TEST_IMPORT_SYMBOL)); assert(mask&(1u<<NG_TEST_IMPORT_NODE)); assert(mask&(1u<<NG_TEST_IMPORT_LABEL)); assert(mask&(1u<<NG_TEST_IMPORT_NODE_PROPERTY)); assert(mask&(1u<<NG_TEST_IMPORT_PARSE_RELATIONSHIPS)); assert(mask&(1u<<NG_TEST_IMPORT_RELATIONSHIP)); assert(mask&(1u<<NG_TEST_IMPORT_RELATIONSHIP_PROPERTY)); assert(mask&(1u<<NG_TEST_IMPORT_COMPLETE)); }

    {ng_graph *o,*q;ng_symbol_id la,lb,lc,k1,k2,k3,k4,rt;ng_node_id x,y;ng_relationship_id re;ng_value pv;size_t before;assert(ng_create(&o,"order1.ng")==NG_OK);assert(ng_symbol(o,"A",&la)==NG_OK);assert(ng_symbol(o,"B",&lb)==NG_OK);assert(ng_symbol(o,"C",&lc)==NG_OK);assert(ng_symbol(o,"k1",&k1)==NG_OK);assert(ng_symbol(o,"k2",&k2)==NG_OK);assert(ng_symbol(o,"k3",&k3)==NG_OK);assert(ng_symbol(o,"k4",&k4)==NG_OK);assert(ng_symbol(o,"R",&rt)==NG_OK);assert(ng_node_create(o,(ng_symbol_id[]){lc,la,lb},3,&x)==NG_OK);assert(ng_node_create(o,0,0,&y)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=1;pv.as.string="x";assert(ng_node_set(o,x,k4,&pv)==NG_OK);pv.as.string="y";assert(ng_node_set(o,x,k2,&pv)==NG_OK);pv.as.string="z";assert(ng_node_set(o,x,k1,&pv)==NG_OK);pv.as.string="w";assert(ng_node_set(o,x,k3,&pv)==NG_OK);assert(ng_relationship_create(o,x,rt,y,&re)==NG_OK);assert(ng_export_property_graph(o,"order-n1.tsv","order-r1.tsv")==NG_OK);before=ng_node_count(o);assert(ng_export_property_graph(o,"order-n2.tsv","order-r2.tsv")==NG_OK);assert(same_file("order-n1.tsv","order-n2.tsv")&&same_file("order-r1.tsv","order-r2.tsv"));assert(ng_node_count(o)==before&&ng_validate(o)==NG_OK);ng_test_fail_after(0);assert(ng_export_property_graph(o,"order-n3.tsv","order-r3.tsv")==NG_OOM);ng_test_fail_reset();assert(ng_export_property_graph(o,"order-n3.tsv","order-r3.tsv")==NG_OK);ng_close(o);remove("order1.ng");remove("order-n1.tsv");remove("order-r1.tsv");remove("order-n2.tsv");remove("order-r2.tsv");remove("order-n3.tsv");remove("order-r3.tsv");(void)q;}
    {FILE*f=fopen("bad.csv","wb");assert(f);fputs("a,R,b\nbad,row\n",f);assert(fclose(f)==0);assert(ng_create(&g,"badcsv.ng")==NG_OK);assert(ng_import_triples_csv(g,"bad.csv",0,&n)==NG_PARSE_ERROR);assert(n==0&&ng_node_count(g)==0&&ng_relationship_count(g)==0);ng_close(g);remove("bad.csv");remove("badcsv.ng");}
    {FILE*f;assert(ng_create(&g,"tail.ng")==NG_OK);assert(ng_save(g)==NG_OK);ng_close(g);f=fopen("tail.ng","ab");assert(f);assert(fputc('x',f)!=EOF);assert(fclose(f)==0);r=0;assert(ng_open(&r,"tail.ng")==NG_CORRUPT);remove("tail.ng");}
    {ng_graph *o,*h;ng_symbol_id person,bin,flag,score,ratio,reltype,relkey,hbin,hscore;ng_node_id x,y;ng_relationship_id re;ng_value pv,got;unsigned char bytes[]={0,1,255};double dv=2.5;size_t matches=0;assert(ng_create(&o,"typed.ng")==NG_OK);assert(ng_symbol(o,"Person",&person)==NG_OK);assert(ng_symbol(o,"bin",&bin)==NG_OK);assert(ng_symbol(o,"flag",&flag)==NG_OK);assert(ng_symbol(o,"score",&score)==NG_OK);assert(ng_symbol(o,"ratio",&ratio)==NG_OK);assert(ng_symbol(o,"KNOWS",&reltype)==NG_OK);assert(ng_symbol(o,"since",&relkey)==NG_OK);assert(ng_node_create(o,&person,1,&x)==NG_OK);assert(ng_node_create(o,0,0,&y)==NG_OK);pv.type=NG_VALUE_BYTES;pv.length=sizeof(bytes);pv.as.bytes=bytes;assert(ng_node_set(o,x,bin,&pv)==NG_OK);pv.type=NG_VALUE_BOOL;pv.length=0;pv.as.boolean=1;assert(ng_node_set(o,x,flag,&pv)==NG_OK);pv.type=NG_VALUE_INT64;pv.as.integer=42;assert(ng_node_set(o,x,score,&pv)==NG_OK);pv.type=NG_VALUE_DOUBLE;memcpy(&pv.as.real,&dv,8);assert(ng_node_set(o,x,ratio,&pv)==NG_OK);assert(ng_relationship_create(o,x,reltype,y,&re)==NG_OK);pv.type=NG_VALUE_INT64;pv.as.integer=2026;assert(ng_relationship_set(o,re,relkey,&pv)==NG_OK);assert(ng_export_property_graph(o,"typed-n.tsv","typed-r.tsv")==NG_OK);assert(ng_create(&h,"typed2.ng")==NG_OK);assert(ng_import_property_graph(h,"typed-n.tsv","typed-r.tsv",0,&n,0)==NG_OK);assert(n==3&&ng_validate(h)==NG_OK);assert(ng_symbol(h,"bin",&hbin)==NG_OK);assert(ng_symbol(h,"score",&hscore)==NG_OK);pv.type=NG_VALUE_BYTES;pv.length=sizeof(bytes);pv.as.bytes=bytes;assert(ng_find_nodes(h,0,hbin,&pv,match_count_cb,&matches)==NG_OK&&matches==1);assert(ng_node_property(h,x,hscore,&got)==NG_OK&&got.type==NG_VALUE_INT64&&got.as.integer==42);ng_close(h);ng_close(o);remove("typed.ng");remove("typed2.ng");remove("typed-n.tsv");remove("typed-r.tsv");}
    {ng_transaction*tx;ng_graph*tg;ng_node_index*ix;ng_symbol_id label,key;ng_node_id x;ng_value pv;size_t matches=0;assert(ng_create(&g,"tx.ng")==NG_OK);assert(ng_symbol(g,"Person",&label)==NG_OK);assert(ng_symbol(g,"name",&key)==NG_OK);assert(ng_node_create(g,&label,1,&x)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=5;pv.as.string="Alice";assert(ng_node_set(g,x,key,&pv)==NG_OK);assert(ng_node_index_build(g,label,key,&ix)==NG_OK);assert(ng_node_index_find(ix,&pv,match_count_cb,&matches)==NG_OK&&matches==1);ng_node_index_free(ix);assert(ng_node_unset(g,x,key)==NG_OK);assert(ng_node_property(g,x,key,&pv)==NG_NOT_FOUND);assert(ng_transaction_begin(g,&tx)==NG_OK);tg=ng_transaction_graph(tx);assert(tg);assert(ng_node_create(tg,&label,1,&a)==NG_OK);ng_transaction_rollback(tx);assert(ng_node_count(g)==1);assert(ng_transaction_begin(g,&tx)==NG_OK);tg=ng_transaction_graph(tx);assert(ng_node_create(tg,&label,1,&a)==NG_OK);assert(ng_transaction_commit(tx)==NG_OK);assert(ng_node_count(g)==2&&ng_validate(g)==NG_OK);ng_close(g);remove("tx.ng");}
    {ng_node_index*ix;ng_symbol_id label,key,out_label,out_key;ng_node_id x;ng_value pv;size_t matches=0;assert(ng_create(&g,"indexmeta.ng")==NG_OK);assert(ng_symbol(g,"Person",&label)==NG_OK);assert(ng_symbol(g,"email",&key)==NG_OK);assert(ng_node_create(g,&label,1,&x)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=13;pv.as.string="a@example.com";assert(ng_node_set(g,x,key,&pv)==NG_OK);assert(ng_node_index_create(g,label,key)==NG_OK);assert(ng_node_index_create(g,label,key)==NG_EXISTS);assert(ng_node_index_count(g)==1);assert(ng_save(g)==NG_OK);ng_close(g);assert(ng_open(&g,"indexmeta.ng")==NG_OK);assert(ng_node_index_count(g)==1);assert(ng_node_index_get(g,0,&out_label,&out_key)==NG_OK&&out_label==label&&out_key==key);assert(ng_node_index_build(g,out_label,out_key,&ix)==NG_OK);assert(ng_node_index_find(ix,&pv,match_count_cb,&matches)==NG_OK&&matches==1);ng_node_index_free(ix);assert(ng_node_index_drop(g,label,key)==NG_OK);assert(ng_node_index_count(g)==0);ng_close(g);remove("indexmeta.ng");}
    {ng_symbol_id person,other,name;ng_node_id x,y,z,first,second;ng_value pv;assert(ng_create(&g,"constraints.ng")==NG_OK);assert(ng_symbol(g,"Person",&person)==NG_OK);assert(ng_symbol(g,"Other",&other)==NG_OK);assert(ng_symbol(g,"name",&name)==NG_OK);assert(ng_node_create(g,&person,1,&x)==NG_OK);assert(ng_node_create(g,&person,1,&y)==NG_OK);assert(ng_node_create(g,&other,1,&z)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=5;pv.as.string="Alice";assert(ng_node_set(g,x,name,&pv)==NG_OK);assert(ng_require_node_property(g,person,name,&first)==NG_NOT_FOUND&&first==y);assert(ng_unique_node_property(g,person,name,&first,&second)==NG_OK);assert(ng_require_node_property(g,other,name,&first)==NG_NOT_FOUND&&first==z);assert(ng_node_set(g,y,name,&pv)==NG_OK);assert(ng_unique_node_property(g,person,name,&first,&second)==NG_EXISTS&&first==x&&second==y);pv.length=3;pv.as.string="Bob";assert(ng_node_set(g,y,name,&pv)==NG_OK);assert(ng_require_node_property(g,person,name,&first)==NG_OK&&first==0);assert(ng_unique_node_property(g,person,name,&first,&second)==NG_OK&&first==0&&second==0);ng_close(g);remove("constraints.ng");}
    {ng_symbol_id person,name,label,key;ng_node_id x,y;ng_value pv;ng_node_constraint_kind kind;assert(ng_create(&g,"schema.ng")==NG_OK);assert(ng_symbol(g,"Person",&person)==NG_OK);assert(ng_symbol(g,"name",&name)==NG_OK);assert(ng_node_create(g,&person,1,&x)==NG_OK);assert(ng_node_create(g,&person,1,&y)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=5;pv.as.string="Alice";assert(ng_node_set(g,x,name,&pv)==NG_OK);pv.length=3;pv.as.string="Bob";assert(ng_node_set(g,y,name,&pv)==NG_OK);assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_REQUIRED_PROPERTY,person,name)==NG_OK);assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_UNIQUE_PROPERTY,person,name)==NG_OK);assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_UNIQUE_PROPERTY,person,name)==NG_EXISTS);assert(ng_node_constraint_count(g)==2);assert(ng_save(g)==NG_OK);ng_close(g);assert(ng_open(&g,"schema.ng")==NG_OK);assert(ng_node_constraint_count(g)==2);assert(ng_node_constraint_get(g,0,&kind,&label,&key)==NG_OK&&kind==NG_NODE_CONSTRAINT_REQUIRED_PROPERTY&&label==person&&key==name);assert(ng_node_constraint_get(g,1,&kind,&label,&key)==NG_OK&&kind==NG_NODE_CONSTRAINT_UNIQUE_PROPERTY&&label==person&&key==name);pv.length=5;pv.as.string="Alice";assert(ng_node_set(g,y,name,&pv)==NG_EXISTS);assert(ng_node_unset(g,x,name)==NG_NOT_FOUND);pv.type=NG_VALUE_NULL;pv.length=0;assert(ng_node_set(g,x,name,&pv)==NG_NOT_FOUND);assert(ng_node_constraint_drop(g,NG_NODE_CONSTRAINT_UNIQUE_PROPERTY,person,name)==NG_OK);pv.type=NG_VALUE_STRING;pv.length=5;pv.as.string="Alice";assert(ng_node_set(g,y,name,&pv)==NG_OK);assert(ng_save(g)==NG_OK);ng_close(g);remove("schema.ng");}
    {ng_symbol_id person,name;ng_node_id x,y;ng_property props[1];ng_value got;assert(ng_create(&g,"create-props.ng")==NG_OK);assert(ng_symbol(g,"Person",&person)==NG_OK);assert(ng_symbol(g,"name",&name)==NG_OK);assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_REQUIRED_PROPERTY,person,name)==NG_OK);assert(ng_node_create_with_properties(g,&person,1,0,0,&x)==NG_NOT_FOUND);assert(ng_node_count(g)==0);props[0].key=name;props[0].value.type=NG_VALUE_STRING;props[0].value.length=5;props[0].value.as.string="Alice";assert(ng_node_create_with_properties(g,&person,1,props,1,&x)==NG_OK);assert(ng_node_property(g,x,name,&got)==NG_OK&&got.type==NG_VALUE_STRING&&got.length==5&&!memcmp(got.as.string,"Alice",5));assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_UNIQUE_PROPERTY,person,name)==NG_OK);assert(ng_node_create_with_properties(g,&person,1,props,1,&y)==NG_EXISTS);assert(ng_node_count(g)==1);props[0].value.length=3;props[0].value.as.string="Bob";assert(ng_node_create_with_properties(g,&person,1,props,1,&y)==NG_OK);assert(ng_node_count(g)==2&&ng_validate(g)==NG_OK);ng_close(g);remove("create-props.ng");}
    {FILE*nf=fopen("constraint-nodes.tsv","wb"),*rf=fopen("constraint-rels.tsv","wb");ng_symbol_id person,name;assert(nf&&rf);fputs("node\ta\tPerson\t\n",nf);assert(fclose(nf)==0);assert(fclose(rf)==0);assert(ng_create(&g,"constraint-import.ng")==NG_OK);assert(ng_symbol(g,"Person",&person)==NG_OK);assert(ng_symbol(g,"name",&name)==NG_OK);assert(ng_node_constraint_create(g,NG_NODE_CONSTRAINT_REQUIRED_PROPERTY,person,name)==NG_OK);assert(ng_import_property_graph(g,"constraint-nodes.tsv","constraint-rels.tsv",0,&n,0)==NG_NOT_FOUND);assert(n==0&&ng_node_count(g)==0&&ng_validate(g)==NG_OK);ng_close(g);remove("constraint-import.ng");remove("constraint-nodes.tsv");remove("constraint-rels.tsv");}
    {FILE*f;assert(ng_create(&g,"backup.ng")==NG_OK);assert(ng_symbol(g,"P",&p)==NG_OK);assert(ng_node_create(g,&p,1,&a)==NG_OK);f=fopen("guard-n.tsv.nautylusbak","wb");assert(f);fputs("backup\n",f);assert(fclose(f)==0);assert(ng_export_property_graph(g,"guard-n.tsv","guard-r.tsv")==NG_EXISTS);ng_close(g);remove("backup.ng");remove("guard-n.tsv.nautylusbak");remove("guard-r.tsv.nautylusbak");remove("guard-n.tsv");remove("guard-r.tsv");}
    {FILE*f=fopen("pipe.tsv","wb");assert(f);fputs("alice\tKNOWS\tbob\n",f);assert(fclose(f)==0);remove("pipe.ng");assert(system(NAUTYLUS_CLI " create pipe.ng > pipe-create.out")==0);assert(system(NAUTYLUS_CLI " open pipe.ng > pipe-open.out")==0);assert(system(NAUTYLUS_CLI " store pipe.ng pipe.tsv > pipe-import.out")==0);assert(system(NAUTYLUS_CLI " analyze pipe.ng > pipe-analyze.out")==0);assert(system(NAUTYLUS_CLI " analyse pipe.ng > pipe-analyse.out")==0);assert(system(NAUTYLUS_CLI " export pipe.ng - > pipe-out.tsv")==0);assert(system(NAUTYLUS_CLI " search pipe.ng 'MATCH (n) RETURN n LIMIT 1' > pipe-search.out")==0);assert(system(NAUTYLUS_CLI " query pipe.ng 'MATCH (n) RETURN n LIMIT 1' > pipe-query.out")==0);assert(system(NAUTYLUS_CLI " explain 'MATCH (n) RETURN n LIMIT 1' > pipe-explain.out")==0);assert(same_file("pipe.tsv","pipe-out.tsv"));remove("pipe.ng");remove("pipe.tsv");remove("pipe-out.tsv");remove("pipe-create.out");remove("pipe-open.out");remove("pipe-import.out");remove("pipe-analyze.out");remove("pipe-analyse.out");remove("pipe-search.out");remove("pipe-query.out");remove("pipe-explain.out");}
    {FILE*f=fopen("nosave.tsv","wb");assert(f);fputs("alice\tKNOWS\tbob\n",f);assert(fclose(f)==0);remove("nosave.ng");assert(system(NAUTYLUS_CLI " create nosave.ng > nosave-create.out")==0);assert(system(NAUTYLUS_CLI " import nosave.ng nosave.tsv > nosave-import.out")==0);assert(system(NAUTYLUS_CLI " stats nosave.ng > nosave-stats.out")==0);f=fopen("nosave-stats.expected","wb");assert(f);fputs("nodes: 0\nrelationships: 0\nsymbols: 0\n",f);assert(fclose(f)==0);assert(same_file("nosave-stats.out","nosave-stats.expected"));remove("nosave.ng");remove("nosave.tsv");remove("nosave-create.out");remove("nosave-import.out");remove("nosave-stats.out");remove("nosave-stats.expected");}
    {FILE*f=fopen("csv.csv","wb");assert(f);fputs("\"ali,ce\",KNOWS,\"bo\"\"b\"\n",f);assert(fclose(f)==0);remove("csv.ng");assert(system(NAUTYLUS_CLI " create csv.ng > csv-create.out")==0);assert(system(NAUTYLUS_CLI " store-csv csv.ng csv.csv > csv-import.out")==0);assert(system(NAUTYLUS_CLI " export csv.ng - > csv-out.tsv")==0);f=fopen("csv-expected.tsv","wb");assert(f);fputs("ali,ce\tKNOWS\tbo\"b\n",f);assert(fclose(f)==0);assert(same_file("csv-expected.tsv","csv-out.tsv"));remove("csv.ng");remove("csv.csv");remove("csv-out.tsv");remove("csv-expected.tsv");remove("csv-create.out");remove("csv-import.out");}
    {assert(system(NAUTYLUS_CLI " help > help.out")==0);assert(system(NAUTYLUS_CLI " --version > version.out")==0);remove("help.out");remove("version.out");}
    {remove("bench.ng");assert(system(NAUTYLUS_CLI " bench bench.ng 128 > bench.out")==0);remove("bench.ng");remove("bench.out");}
    {FILE*nf=fopen("cli-nodes.tsv","wb"),*rf=fopen("cli-rels.tsv","wb");assert(nf&&rf);fputs("node\ta\tCli\tname=s:416c696365\nnode\tb\tCli\tname=s:426f62\nnode\tc\tCli\tname=s:4361726c\n",nf);fputs("relationship\tr\ta\tKNOWS\tb\tsince=i:2026\nrelationship\tr2\tb\tKNOWS\tc\tsince=i:2027\n",rf);assert(fclose(nf)==0);assert(fclose(rf)==0);remove("cli.ng");remove("cli2.ng");assert(system(NAUTYLUS_CLI " create cli.ng > cli-create.out")==0);assert(system(NAUTYLUS_CLI " store-ng cli.ng cli-nodes.tsv cli-rels.tsv > cli-import.out")==0);assert(system(NAUTYLUS_CLI " constraint-require cli.ng Cli name > cli-constraint-require.out")==0);assert(system(NAUTYLUS_CLI " constraint-unique cli.ng Cli name > cli-constraint-unique.out")==0);assert(system(NAUTYLUS_CLI " constraints cli.ng > cli-constraints.out")==0);{FILE*ef=fopen("cli-constraints.expected","wb");assert(ef);fputs("required Cli name\nunique Cli name\n",ef);assert(fclose(ef)==0);assert(same_file("cli-constraints.out","cli-constraints.expected"));remove("cli-constraints.expected");}assert(system(NAUTYLUS_CLI " index-create cli.ng Cli name > cli-index-create.out")==0);assert(system(NAUTYLUS_CLI " indexes cli.ng > cli-indexes.out")==0);{FILE*ef=fopen("cli-indexes.expected","wb");assert(ef);fputs("node Cli name\n",ef);assert(fclose(ef)==0);assert(same_file("cli-indexes.out","cli-indexes.expected"));remove("cli-indexes.expected");}assert(system(NAUTYLUS_CLI " validate cli.ng > cli-validate.out")==0);assert(system(NAUTYLUS_CLI " stats cli.ng > cli-stats.out")==0);assert(system(NAUTYLUS_CLI " search cli.ng 'MATCH (n:Cli) WHERE n.name = \"Alice\" RETURN n.name LIMIT 1' > cli-search.out")==0);assert(system(NAUTYLUS_CLI " search cli.ng 'MATCH (n:Cli)-[:KNOWS*2]->(m:Cli) RETURN n.name, m.name LIMIT 1' > cli-multi.out")==0);{FILE*ef=fopen("cli-multi.expected","wb");assert(ef);fputs("Alice	Carl\n",ef);assert(fclose(ef)==0);assert(same_file("cli-multi.out","cli-multi.expected"));remove("cli-multi.expected");}assert(system(NAUTYLUS_CLI " export-ng cli.ng cli-export-nodes.tsv cli-export-rels.tsv")==0);assert(system(NAUTYLUS_CLI " create cli2.ng > cli2-create.out")==0);assert(system(NAUTYLUS_CLI " store-ng cli2.ng cli-export-nodes.tsv cli-export-rels.tsv > cli2-import.out")==0);assert(system(NAUTYLUS_CLI " validate cli2.ng > cli2-validate.out")==0);remove("cli.ng");remove("cli2.ng");remove("cli-nodes.tsv");remove("cli-rels.tsv");remove("cli-export-nodes.tsv");remove("cli-export-rels.tsv");remove("cli-create.out");remove("cli-import.out");remove("cli-constraint-require.out");remove("cli-constraint-unique.out");remove("cli-constraints.out");remove("cli-index-create.out");remove("cli-indexes.out");remove("cli-validate.out");remove("cli-stats.out");remove("cli-search.out");remove("cli-multi.out");remove("cli2-create.out");remove("cli2-import.out");remove("cli2-validate.out");}
    {
        FILE *nf=fopen("bool-nodes.tsv","wb"), *rf=fopen("bool-rels.tsv","wb"), *ef;
        assert(nf&&rf);
        fputs("node\ta\tPerson\tname=s:416c696365\nnode\tb\tPerson\tname=s:426f62\nnode\tc\tPerson\tname=s:4361726c\n",nf);
        fputs("relationship\tr\ta\tKNOWS\tb\tsince=i:2026\n",rf);
        assert(fclose(nf)==0);
        assert(fclose(rf)==0);
        remove("bool.ng");
        assert(system(NAUTYLUS_CLI " create bool.ng > bool-create.out")==0);
        assert(system(NAUTYLUS_CLI " store-ng bool.ng bool-nodes.tsv bool-rels.tsv > bool-store.out")==0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" OR n.name = \"Alice\" RETURN n.name' > bool-or.out")==0);
        ef=fopen("bool-or.expected","wb"); assert(ef); fputs("Alice\nBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-or.out","bool-or.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person {name: \"Alice\"}) RETURN person.name' > bool-node-map.out")==0);
        ef=fopen("bool-node-map.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-node-map.out","bool-node-map.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person {name: \"Alice\"}) WHERE person.name = \"Bob\" RETURN person.name' > bool-node-map-where.out")==0);
        ef=fopen("bool-node-map-where.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-node-map-where.out","bool-node-map-where.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (:Person {name: \"Alice\"}) MATCH (friend:Person {name: \"Bob\"}) RETURN friend.name LIMIT 1' > bool-anon-node.out")==0);
        ef=fopen("bool-anon-node.expected","wb"); assert(ef); fputs("Bob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-anon-node.out","bool-anon-node.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person {name \"Alice\"}) RETURN person.name' > bool-bad-node-map.out 2> bool-bad-node-map.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name IN [\"Bob\", \"Alice\"] RETURN n.name' > bool-in.out")==0);
        ef=fopen("bool-in.expected","wb"); assert(ef); fputs("Alice\nBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-in.out","bool-in.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE (n.name = \"Bob\" OR n.name = \"Alice\") AND n.name = \"Alice\" RETURN n.name' > bool-paren.out")==0);
        ef=fopen("bool-paren.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-paren.out","bool-paren.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE NOT (n.name = \"Bob\" OR n.name = \"Alice\") RETURN n.name' > bool-not.out")==0);
        ef=fopen("bool-not.expected","wb"); assert(ef); fputs("Carl\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-not.out","bool-not.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE NOT RETURN n.name' > bool-badnot.out 2> bool-badnot.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.nickname IS NULL RETURN person.name' > bool-is-null.out")==0);
        ef=fopen("bool-is-null.expected","wb"); assert(ef); fputs("Alice\nBob\nCarl\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-is-null.out","bool-is-null.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name IS NOT NULL RETURN person.name' > bool-is-not-null.out")==0);
        assert(same_file("bool-is-not-null.out","bool-is-null.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name IS EMPTY RETURN person.name' > bool-bad-is-null.out 2> bool-bad-is-null.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE (n.name = \"Bob\" OR n.name = \"Alice\" RETURN n.name' > bool-badparen.out 2> bool-badparen.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name <> \"Carl\" RETURN n.name' > bool-ne.out")==0);
        ef=fopen("bool-ne.expected","wb"); assert(ef); fputs("Alice\nBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-ne.out","bool-ne.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name > \"Bob\" RETURN n.name' > bool-gt.out")==0);
        ef=fopen("bool-gt.expected","wb"); assert(ef); fputs("Carl\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-gt.out","bool-gt.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE id(n) >= 2 RETURN n.name' > bool-ge.out")==0);
        ef=fopen("bool-ge.expected","wb"); assert(ef); fputs("Bob\nCarl\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-ge.out","bool-ge.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name != \"Bob\" RETURN n.name' > bool-badcmp.out 2> bool-badcmp.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name ORDER BY n.name DESC SKIP 1 LIMIT 1' > bool-order.out")==0);
        ef=fopen("bool-order.expected","wb"); assert(ef); fputs("Bob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-order.out","bool-order.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name AS name ORDER BY n.name LIMIT 1' > bool-alias.out")==0);
        ef=fopen("bool-alias.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-alias.out","bool-alias.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) RETURN n.name AS ORDER BY n.name' > bool-badalias.out 2> bool-badalias.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[]->(m) RETURN m.name ORDER BY m.name' > bool-badorder.out 2> bool-badorder.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2026}]->(m:Person) WHERE r.since >= 2020 RETURN n.name, r.since, m.name' > bool-relprop.out")==0);
        ef=fopen("bool-relprop.expected","wb"); assert(ef); fputs("Alice\t2026\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relprop.out","bool-relprop.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person {name: \"Alice\"})-[knows:KNOWS {since: 2026}]->(friend:Person {name: \"Bob\"}) RETURN person.name, knows.since, friend.name' > bool-rel-node-map.out")==0);
        assert(same_file("bool-rel-node-map.out","bool-relprop.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person)-[knows:KNOWS]->(friend:Person) WHERE knows.missing IS NULL AND knows.since IS NOT NULL RETURN person.name, friend.name' > bool-rel-is-null.out")==0);
        ef=fopen("bool-rel-is-null.expected","wb"); assert(ef); fputs("Alice\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-rel-is-null.out","bool-rel-is-null.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2025}]->(m:Person) RETURN n.name' > bool-relprop-empty.out")==0);
        ef=fopen("bool-relprop-empty.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-relprop-empty.out","bool-relprop-empty.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE r.since = 2026 RETURN n.name' > bool-badrelvar.out 2> bool-badrelvar.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2026}]->(m:Person) SET r.strength = 7 RETURN r.strength' > bool-relset.out")==0);
        ef=fopen("bool-relset.expected","wb"); assert(ef); fputs("7\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relset.out","bool-relset.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE r.strength = 7 RETURN n.name, m.name' > bool-relset-search.out")==0);
        ef=fopen("bool-relset-search.expected","wb"); assert(ef); fputs("Alice\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relset-search.out","bool-relset-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS*1..2]->(m:Person) SET r.bad = 1' > bool-badrelset.out 2> bool-badrelset.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE r.strength = 7 DELETE r' > bool-reldelete.out")==0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person)-[r:KNOWS]->(m:Person) WHERE r.strength = 7 RETURN n.name, m.name' > bool-reldelete-search.out")==0);
        ef=fopen("bool-reldelete-search.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-reldelete-search.out","bool-reldelete-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS*1..2]->(m:Person) DELETE r' > bool-badreldel.out 2> bool-badreldel.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name = \"Alice\" AND friend.name = \"Bob\" MERGE (person)-[knows:KNOWS {since: 2030}]->(friend) RETURN knows.since' > bool-relmerge.out")==0);
        ef=fopen("bool-relmerge.expected","wb"); assert(ef); fputs("2030\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relmerge.out","bool-relmerge.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name = \"Alice\" AND friend.name = \"Bob\" MERGE (person)-[knows:KNOWS {since: 2030}]->(friend) RETURN knows.since' > bool-relmerge-again.out")==0);
        assert(same_file("bool-relmerge-again.out","bool-relmerge.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2030}]->(m:Person) RETURN r.since' > bool-relmerge-search.out")==0);
        assert(same_file("bool-relmerge-search.out","bool-relmerge.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person)-[knows:KNOWS {since: 2030}]->(friend:Person) SET knows.weight = 9 RETURN knows.weight' > bool-relset-var.out")==0);
        ef=fopen("bool-relset-var.expected","wb"); assert(ef); fputs("9\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relset-var.out","bool-relset-var.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (person:Person)-[knows:KNOWS {since: 2030}]->(friend:Person) WHERE knows.weight = 9 RETURN person.name, friend.name' > bool-relset-var-search.out")==0);
        ef=fopen("bool-relset-var-search.expected","wb"); assert(ef); fputs("Alice\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-relset-var-search.out","bool-relset-var-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" MERGE (n)-[r:KNOWS]->(m)' > bool-badrelmerge.out 2> bool-badrelmerge.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)<-[r:KNOWS {since: 2030}]-(m:Person) WHERE n.name = \"Bob\" RETURN m.name, r.since, n.name' > bool-incoming.out")==0);
        ef=fopen("bool-incoming.expected","wb"); assert(ef); fputs("Alice\t2030\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-incoming.out","bool-incoming.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)-[r:KNOWS {since: 2030}]-(m:Person) WHERE n.name = \"Bob\" RETURN n.name, m.name' > bool-undirected.out")==0);
        ef=fopen("bool-undirected.expected","wb"); assert(ef); fputs("Bob\tAlice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-undirected.out","bool-undirected.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person)<-[r:KNOWS]->(m:Person) RETURN n.name' > bool-baddir.out 2> bool-baddir.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) MATCH (m:Person) WHERE n.name = \"Alice\" AND m.name = \"Bob\" RETURN n.name, m.name' > bool-multimatch.out")==0);
        ef=fopen("bool-multimatch.expected","wb"); assert(ef); fputs("Alice\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-multimatch.out","bool-multimatch.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) MATCH (m:Person) RETURN m.name SKIP 1 LIMIT 1' > bool-multimatch-skip.out")==0);
        ef=fopen("bool-multimatch-skip.expected","wb"); assert(ef); fputs("Bob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-multimatch-skip.out","bool-multimatch-skip.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) MATCH (x:Person) WHERE x.name = \"Bob\" RETURN n.name, x.name LIMIT 1' > bool-varmatch.out")==0);
        ef=fopen("bool-varmatch.expected","wb"); assert(ef); fputs("Alice\tBob\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-varmatch.out","bool-varmatch.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) MATCH (x:Person) RETURN z.name' > bool-badmultimatch.out 2> bool-badmultimatch.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" AND n.name = \"Alice\" RETURN n.name' > bool-and.out")==0);
        ef=fopen("bool-and.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-and.out","bool-and.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'CREATE (person:Person {name: \"Dana\", age: 30}) RETURN person.name' > bool-create-node.out")==0);
        ef=fopen("bool-create-node.expected","wb"); assert(ef); fputs("Dana\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-create-node.out","bool-create-node.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Dana\" RETURN n.name' > bool-create-node-search.out")==0);
        assert(same_file("bool-create-node-search.out","bool-create-node.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name = \"Alice\" AND friend.name = \"Dana\" CREATE (person)-[edge:KNOWS]->(friend) RETURN person.name, friend.name' > bool-create-rel.out")==0);
        ef=fopen("bool-create-rel.expected","wb"); assert(ef); fputs("Alice\tDana\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-create-rel.out","bool-create-rel.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person)-[:KNOWS]->(m:Person) WHERE m.name = \"Dana\" RETURN n.name, m.name' > bool-create-rel-search.out")==0);
        assert(same_file("bool-create-rel-search.out","bool-create-rel.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) MATCH (friend:Person) WHERE person.name = \"Alice\" AND friend.name = \"Dana\" CREATE (n)-[:KNOWS]->(friend)' > bool-badcreaterel-var.out 2> bool-badcreaterel-var.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person)-[edge:KNOWS]->(friend:Person) WHERE friend.name = \"Dana\" DELETE edge' > bool-reldelete-var.out")==0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (person:Person)-[edge:KNOWS]->(friend:Person) WHERE friend.name = \"Dana\" RETURN person.name, friend.name' > bool-reldelete-var-search.out")==0);
        ef=fopen("bool-reldelete-var-search.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-reldelete-var-search.out","bool-reldelete-var-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" CREATE (n)-[:KNOWS]->(m)' > bool-badcreaterel.out 2> bool-badcreaterel.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name = \"Alice\" SET person.city = \"Berlin\" RETURN person.city' > bool-set.out")==0);
        ef=fopen("bool-set.expected","wb"); assert(ef); fputs("Berlin\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-set.out","bool-set.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.city = \"Berlin\" RETURN n.name' > bool-set-search.out")==0);
        ef=fopen("bool-set-search.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-set-search.out","bool-set-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MERGE (person:Person {name: \"Alice\"}) RETURN person.name' > bool-merge-existing.out")==0);
        ef=fopen("bool-merge-existing.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-merge-existing.out","bool-merge-existing.expected"));
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Alice\" RETURN n.name' > bool-merge-existing-search.out")==0);
        assert(same_file("bool-merge-existing-search.out","bool-merge-existing.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MERGE (person:Person {name: \"Eve\"}) RETURN person.name' > bool-merge-new.out")==0);
        ef=fopen("bool-merge-new.expected","wb"); assert(ef); fputs("Eve\n",ef); assert(fclose(ef)==0);
        assert(same_file("bool-merge-new.out","bool-merge-new.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'MATCH (person:Person) WHERE person.name = \"Bob\" DELETE person' > bool-delete.out")==0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"Bob\" RETURN n.name' > bool-delete-search.out")==0);
        ef=fopen("bool-delete-search.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-delete-search.out","bool-delete-search.expected"));
        assert(system(NAUTYLUS_CLI " query bool.ng 'CREATE (n:Person {name \"Bad\"}) RETURN n.name' > bool-badcreate.out 2> bool-badcreate.err")!=0);
        assert(system(NAUTYLUS_CLI " query bool.ng 'CREATE (n:Person {name: \"BadTail\"}) RETURN x.name' > bool-badcreate-tail.out 2> bool-badcreate-tail.err")!=0);
        assert(system(NAUTYLUS_CLI " search bool.ng 'MATCH (n:Person) WHERE n.name = \"BadTail\" RETURN n.name' > bool-badcreate-tail-search.out")==0);
        ef=fopen("bool-badcreate-tail-search.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("bool-badcreate-tail-search.out","bool-badcreate-tail-search.expected"));
        remove("bool.ng"); remove("bool-nodes.tsv"); remove("bool-rels.tsv");
        remove("bool-create.out"); remove("bool-store.out");
        remove("bool-or.out"); remove("bool-or.expected");
        remove("bool-node-map.out"); remove("bool-node-map.expected");
        remove("bool-node-map-where.out"); remove("bool-node-map-where.expected");
        remove("bool-anon-node.out"); remove("bool-anon-node.expected");
        remove("bool-bad-node-map.out"); remove("bool-bad-node-map.err");
        remove("bool-in.out"); remove("bool-in.expected");
        remove("bool-paren.out"); remove("bool-paren.expected");
        remove("bool-not.out"); remove("bool-not.expected");
        remove("bool-badnot.out"); remove("bool-badnot.err");
        remove("bool-is-null.out"); remove("bool-is-null.expected");
        remove("bool-is-not-null.out");
        remove("bool-bad-is-null.out"); remove("bool-bad-is-null.err");
        remove("bool-badparen.out"); remove("bool-badparen.err");
        remove("bool-ne.out"); remove("bool-ne.expected");
        remove("bool-gt.out"); remove("bool-gt.expected");
        remove("bool-ge.out"); remove("bool-ge.expected");
        remove("bool-badcmp.out"); remove("bool-badcmp.err");
        remove("bool-order.out"); remove("bool-order.expected");
        remove("bool-alias.out"); remove("bool-alias.expected");
        remove("bool-badalias.out"); remove("bool-badalias.err");
        remove("bool-badorder.out"); remove("bool-badorder.err");
        remove("bool-relprop.out"); remove("bool-relprop.expected");
        remove("bool-rel-node-map.out");
        remove("bool-rel-is-null.out"); remove("bool-rel-is-null.expected");
        remove("bool-relprop-empty.out"); remove("bool-relprop-empty.expected");
        remove("bool-badrelvar.out"); remove("bool-badrelvar.err");
        remove("bool-relset.out"); remove("bool-relset.expected");
        remove("bool-relset-search.out"); remove("bool-relset-search.expected");
        remove("bool-badrelset.out"); remove("bool-badrelset.err");
        remove("bool-reldelete.out"); remove("bool-reldelete-search.out"); remove("bool-reldelete-search.expected");
        remove("bool-badreldel.out"); remove("bool-badreldel.err");
        remove("bool-relmerge.out"); remove("bool-relmerge.expected"); remove("bool-relmerge-again.out");
        remove("bool-relmerge-search.out"); remove("bool-badrelmerge.out"); remove("bool-badrelmerge.err");
        remove("bool-relset-var.out"); remove("bool-relset-var.expected");
        remove("bool-relset-var-search.out"); remove("bool-relset-var-search.expected");
        remove("bool-incoming.out"); remove("bool-incoming.expected");
        remove("bool-undirected.out"); remove("bool-undirected.expected");
        remove("bool-baddir.out"); remove("bool-baddir.err");
        remove("bool-varmatch.out"); remove("bool-varmatch.expected");
        remove("bool-multimatch.out"); remove("bool-multimatch.expected");
        remove("bool-multimatch-skip.out"); remove("bool-multimatch-skip.expected");
        remove("bool-badmultimatch.out"); remove("bool-badmultimatch.err");
        remove("bool-and.out"); remove("bool-and.expected");
        remove("bool-create-node.out"); remove("bool-create-node.expected"); remove("bool-create-node-search.out");
        remove("bool-create-rel.out"); remove("bool-create-rel.expected"); remove("bool-create-rel-search.out");
        remove("bool-badcreaterel-var.out"); remove("bool-badcreaterel-var.err");
        remove("bool-reldelete-var.out"); remove("bool-reldelete-var-search.out"); remove("bool-reldelete-var-search.expected");
        remove("bool-badcreaterel.out"); remove("bool-badcreaterel.err");
        remove("bool-set.out"); remove("bool-set.expected"); remove("bool-set-search.out"); remove("bool-set-search.expected");
        remove("bool-merge-existing.out"); remove("bool-merge-existing.expected"); remove("bool-merge-existing-search.out");
        remove("bool-merge-new.out"); remove("bool-merge-new.expected");
        remove("bool-delete.out"); remove("bool-delete-search.out"); remove("bool-delete-search.expected");
        remove("bool-badcreate.out"); remove("bool-badcreate.err");
        remove("bool-badcreate-tail.out"); remove("bool-badcreate-tail.err");
        remove("bool-badcreate-tail-search.out"); remove("bool-badcreate-tail-search.expected");
    }
    {
        FILE *nf=fopen("chain-nodes.tsv","wb"), *rf=fopen("chain-rels.tsv","wb"), *ef;
        assert(nf&&rf);
        fputs("node\ta\tPerson\tname=s:416c696365;age=i:40\nnode\tb\tPerson\tname=s:426f62\nnode\tc\tPerson\tname=s:4361726c\nnode\tx\tPerson\tname=s:586176696572\nnode\td\tPerson\tname=s:44616e61\n",nf);
        fputs("relationship\tr1\ta\tKNOWS\tb\tsince=i:1\nrelationship\tr2\ta\tKNOWS\tx\tsince=i:2\nrelationship\tr3\tb\tWORKS_WITH\tc\tweight=i:3\nrelationship\tr4\tx\tWORKS_WITH\td\tweight=i:4\n",rf);
        assert(fclose(nf)==0);
        assert(fclose(rf)==0);
        remove("chain.ng");
        assert(system(NAUTYLUS_CLI " create chain.ng > chain-create.out")==0);
        assert(system(NAUTYLUS_CLI " store-ng chain.ng chain-nodes.tsv chain-rels.tsv > chain-store.out")==0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[r1:KNOWS]->(b)-[r2:WORKS_WITH]->(c) RETURN a.name, b.name, c.name' > chain-path.out")==0);
        ef=fopen("chain-path.expected","wb"); assert(ef); fputs("Alice\tBob\tCarl\nAlice\tXavier\tDana\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-path.out","chain-path.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[r1:KNOWS]->(b)-[r2:WORKS_WITH]->(c) WHERE a.name = \"Alice\" AND b.name = \"Bob\" AND r2.weight = 3 RETURN a.name, r1.since, b.name, r2.weight, c.name' > chain-where.out")==0);
        ef=fopen("chain-where.expected","wb"); assert(ef); fputs("Alice\t1\tBob\t3\tCarl\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-where.out","chain-where.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"})-[r1:KNOWS]->(b) MATCH (b)-[r2:WORKS_WITH]->(c) RETURN a.name, b.name, c.name' > chain-multimatch.out")==0);
        assert(same_file("chain-multimatch.out","chain-path.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->()-[:WORKS_WITH]->(c) RETURN c.name' > chain-anon.out")==0);
        ef=fopen("chain-anon.expected","wb"); assert(ef); fputs("Carl\nDana\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-anon.out","chain-anon.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1, \"hello\", 2 + 3 * 4, (2 + 3) * 4, a.age + 1, -a.age + 50 LIMIT 1' > chain-return-expr.out")==0);
        ef=fopen("chain-return-expr.expected","wb"); assert(ef); fputs("1\thello\t14\t20\t41\t10\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-return-expr.out","chain-return-expr.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN DISTINCT a.name' > chain-distinct-one.out")==0);
        ef=fopen("chain-distinct-one.expected","wb"); assert(ef); fputs("Alice\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-distinct-one.out","chain-distinct-one.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN DISTINCT a.name, b.name' > chain-distinct-two.out")==0);
        ef=fopen("chain-distinct-two.expected","wb"); assert(ef); fputs("Alice\tBob\nAlice\tXavier\n",ef); assert(fclose(ef)==0);
        assert(same_file("chain-distinct-two.out","chain-distinct-two.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN DISTINCT a.age + 1 SKIP 1' > chain-distinct-skip.out")==0);
        ef=fopen("chain-distinct-skip.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("chain-distinct-skip.out","chain-distinct-skip.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person) RETURN DISTINCT' > chain-bad-distinct.out 2> chain-bad-distinct.err")!=0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1 / 0' > chain-bad-div.out 2> chain-bad-div.err")!=0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person {name: \"Alice\"}) RETURN 1 + \"x\"' > chain-bad-add.out 2> chain-bad-add.err")!=0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[:LIKES]->(b)-[:WORKS_WITH]->(c) RETURN a.name, c.name' > chain-empty.out")==0);
        ef=fopen("chain-empty.expected","wb"); assert(ef); assert(fclose(ef)==0);
        assert(same_file("chain-empty.out","chain-empty.expected"));
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person)-[:KNOWS]->(b)-[:WORKS_WITH]->(c) RETURN z.name' > chain-bad-var.out 2> chain-bad-var.err")!=0);
        assert(system(NAUTYLUS_CLI " query chain.ng 'MATCH (a:Person:Employee)-[:KNOWS]->(b) RETURN a.name' > chain-bad-label.out 2> chain-bad-label.err")!=0);
        remove("chain.ng"); remove("chain-nodes.tsv"); remove("chain-rels.tsv");
        remove("chain-create.out"); remove("chain-store.out");
        remove("chain-path.out"); remove("chain-path.expected");
        remove("chain-where.out"); remove("chain-where.expected");
        remove("chain-multimatch.out");
        remove("chain-anon.out"); remove("chain-anon.expected");
        remove("chain-return-expr.out"); remove("chain-return-expr.expected");
        remove("chain-distinct-one.out"); remove("chain-distinct-one.expected");
        remove("chain-distinct-two.out"); remove("chain-distinct-two.expected");
        remove("chain-distinct-skip.out"); remove("chain-distinct-skip.expected");
        remove("chain-bad-distinct.out"); remove("chain-bad-distinct.err");
        remove("chain-bad-div.out"); remove("chain-bad-div.err");
        remove("chain-bad-add.out"); remove("chain-bad-add.err");
        remove("chain-empty.out"); remove("chain-empty.expected");
        remove("chain-bad-var.out"); remove("chain-bad-var.err");
        remove("chain-bad-label.out"); remove("chain-bad-label.err");
    }
    remove_import_files(); remove("test.ng"); puts("ok"); return 0;
}
