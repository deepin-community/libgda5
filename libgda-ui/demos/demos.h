typedef	GtkWidget *(*GDoDemoFunc) (GtkWidget *do_widget);

typedef struct _Demo Demo;

struct _Demo 
{
  gchar *title;
  gchar *filename;
  GDoDemoFunc func;
  Demo *children;
};

GtkWidget *do_basic_form (GtkWidget *do_widget);
GtkWidget *do_data_model_dir (GtkWidget *do_widget);
GtkWidget *do_form (GtkWidget *do_widget);
GtkWidget *do_form_rw (GtkWidget *do_widget);
GtkWidget *do_form_pict (GtkWidget *do_widget);
GtkWidget *do_form_data_layout (GtkWidget *do_widget);
GtkWidget *do_form_model_change (GtkWidget *do_widget);
GtkWidget *do_grid (GtkWidget *do_widget);
GtkWidget *do_grid_rw (GtkWidget *do_widget);
GtkWidget *do_grid_pict (GtkWidget *do_widget);
GtkWidget *do_grid_data_layout (GtkWidget *do_widget);
GtkWidget *do_grid_model_change (GtkWidget *do_widget);
GtkWidget *do_linked_grid_form (GtkWidget *do_widget);
GtkWidget *do_linked_model_param (GtkWidget *do_widget);
GtkWidget *do_ddl_queries (GtkWidget *do_widget);
GtkWidget *do_login (GtkWidget *do_widget);
GtkWidget *do_provider_sel (GtkWidget *do_widget);
GtkWidget *do_tree (GtkWidget *do_widget);
GtkWidget *do_cloud (GtkWidget *do_widget);
GtkWidget *do_combo (GtkWidget *do_widget);

Demo child0[] = {
  { "Basic form", "basic_form.c", do_basic_form, NULL },
  { "Read-only form", "form.c", do_form, NULL },
  { "Read-write form", "form_rw.c", do_form_rw, NULL },
  { "Using the picture plugin", "form_pict.c", do_form_pict, NULL },
  { "Custom layout", "form_data_layout.c", do_form_data_layout, NULL },
  { "Changing data model", "form_model_change.c", do_form_model_change, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo child1[] = {
  { "Directory data model", "data_model_dir.c", do_data_model_dir, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo child2[] = {
  { "Read-only grid", "grid.c", do_grid, NULL },
  { "Read-write grid", "grid_rw.c", do_grid_rw, NULL },
  { "Using the picture plugin", "grid_pict.c", do_grid_pict, NULL },
  { "Custom layout", "grid_data_layout.c", do_grid_data_layout, NULL },
  { "Changing data model", "grid_model_change.c", do_grid_model_change, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo child3[] = {
  { "Same data", "linked_grid_form.c", do_linked_grid_form, NULL },
  { "Data model with parameters", "linked_model_param.c", do_linked_model_param, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo child4[] = {
  { "DDL queries", "ddl_queries.c", do_ddl_queries, NULL },
  { "Login", "login.c", do_login, NULL },
  { "Provider selector", "provider_sel.c", do_provider_sel, NULL },
  { "GdaTree display", "tree.c", do_tree, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo child5[] = {
  { "Cloud widget", "cloud.c", do_cloud, NULL },
  { "Combo widget", "combo.c", do_combo, NULL },
  { NULL, NULL, NULL, NULL } 
};

Demo gdaui_demos[] = {
  { "Data models", NULL, NULL, child1 }, 
  { "Data widgets linking", NULL, NULL, child3 }, 
  { "Forms", NULL, NULL, child0 }, 
  { "Grids", NULL, NULL, child2 }, 
  { "Selector widgets", NULL, NULL, child5 }, 
  { "Widgets", NULL, NULL, child4 },
  { NULL, NULL, NULL, NULL } 
};
