#include <glib.h>
#include <stdio.h>
int main(void)
{
	GString *s = g_string_new("hello");
	g_string_append(s, " world");
	GList *l = NULL;
	int i;
	for (i = 0; i < 200; i++) {
		l = g_list_prepend(l, g_strdup_printf("item-%d-padpadpadpad", i));
	}
	GHashTable *h = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(h, (gpointer)"k", (gpointer)"v");
	printf("GLIBTEST-OK len=%zu listlen=%u hv=%s\n", s->len, g_list_length(l), (char *)g_hash_table_lookup(h, "k"));
	g_list_free_full(l, g_free);
	g_string_free(s, TRUE);
	g_hash_table_destroy(h);
	printf("GLIBTEST-FREED-CLEAN\n");
	return 0;
}
