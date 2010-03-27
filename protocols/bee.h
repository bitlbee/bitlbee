typedef struct bee_ui
{
	void *data;
} bee_ui_t;

typedef struct bee
{
	struct set *set;
	
	GSList *users;
	GSList *accounts;
	
	//const bee_ui_funcs_t *ui;
	void *ui_data;
} bee_t;

bee_t *bee_new();
void bee_free( bee_t *b );
