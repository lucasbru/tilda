#include <glib/gi18n.h>

#include "configsys.h"
#include "debug.h"
#include "key_grabber.h"
#include "tilda-keybinding.h"

enum keybinding_columns
{
    KB_TREE_ACTION,
    KB_TREE_SHORTCUT,
    KB_TREE_CONFIG_NAME,
    KB_NUM_COLUMNS
};

struct _TildaKeybindingTreeView {
    GtkWidget    *tree_view;
    GtkListStore *list_store;
    GtkBuilder   *builder;

    /* Stores the signal handler id for the 'button-press-event'
     * of the tree view. */
    gulong handler_id;
};

static gboolean
keybinding_button_press_event_cb (GtkWidget *tree_view,
                                  GdkEventButton *event,
                                  TildaKeybindingTreeView *keybindings);

static gboolean
keybinding_dialog_key_press_event_cb (GtkWidget *dialog,
                                      GdkEventKey *event,
                                      TildaKeybindingTreeView *keybinding);

static gboolean
validate_keybindings (TildaKeybindingTreeView *keybindings,
                      tilda_window *tw);

static gboolean
validate_pulldown_keybinding (const gchar* accel,
                              tilda_window* tw,
                              const gchar* message);

static gboolean
validate_keybinding (const gchar* accel,
                     tilda_window *tw,
                     const gchar* message);

static void
init_bindings_from_config (GtkListStore *list_store,
                           GtkTreeIter *iter);

void
tilda_keybinding_apply (TildaKeybindingTreeView *keybinding)
{
    GtkListStore *list_store;
    GtkTreeIter iter;
    gboolean valid;

    list_store = keybinding->list_store;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store),
                                           &iter);

    while (valid) {
        gchar * action, *config_name, *shortcut, *path;

        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
                            KB_TREE_ACTION, &action,
                            KB_TREE_CONFIG_NAME, &config_name,
                            KB_TREE_SHORTCUT, &shortcut,
                            -1);

        path = g_strdup_printf ("<tilda>/context/%s", action);

        tilda_window_update_keyboard_accelerators (path,
                                                   config_getstr (config_name));

        g_free (path);

        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store),
                                          &iter);
    }
}

TildaKeybindingTreeView*
tilda_keybinding_init (GtkBuilder *builder)
{
    TildaKeybindingTreeView *keybindings =
            g_malloc (sizeof (TildaKeybindingTreeView));

    GtkWidget *tree_view;
    GtkListStore *list_store;

    tree_view = GTK_WIDGET (gtk_builder_get_object (builder,
                                                    "tree_view_keybindings"));

    list_store = gtk_list_store_new (KB_NUM_COLUMNS,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING);

    keybindings->list_store = list_store;
    keybindings->tree_view = tree_view;
    keybindings->builder = builder;

    gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view),
                             GTK_TREE_MODEL (list_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *column;
    GtkTreeIter iter;

    column = gtk_tree_view_column_new_with_attributes (_ ("Action"), renderer,
                                                       "text", KB_TREE_ACTION,
                                                       NULL);

    gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

    column = gtk_tree_view_column_new_with_attributes (_ ("Shortcut"), renderer,
                                                       "text", KB_TREE_SHORTCUT,
                                                       NULL);

    gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);

    init_bindings_from_config (list_store, &iter);

    keybindings->handler_id =
            g_signal_connect (tree_view, "button-press-event",
                              G_CALLBACK (keybinding_button_press_event_cb),
                              keybindings);

    return keybindings;
}

void
tilda_keybinding_free (TildaKeybindingTreeView *keybinding)
{
    g_signal_handler_disconnect (keybinding->tree_view,
                                 keybinding->handler_id);

    keybinding->builder = NULL;
    keybinding->list_store = NULL;
    keybinding->tree_view = NULL;

    g_free (keybinding);
}

gboolean
tilda_keybinding_save (TildaKeybindingTreeView *keybinding,
                       tilda_window *tw)
{
    GtkListStore *list_store;
    GtkTreeIter iter;
    gboolean valid;

    if (!validate_keybindings (keybinding, tw)) {
        return FALSE;
    }

    list_store = keybinding->list_store;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store),
                                           &iter);

    while (valid) {
        gchar * config_name, *shortcut;

        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
                            KB_TREE_CONFIG_NAME, &config_name,
                            KB_TREE_SHORTCUT, &shortcut,
                            -1);

        config_setstr (config_name, shortcut);

        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store),
                                          &iter);
    }

    return TRUE;
}

void
tilda_keybinding_show_invalid_keybinding_dialog (GtkWindow *parent_window,
                                                 const gchar* message)
{
    DEBUG_FUNCTION ("show_invalid_keybinding_dialog");

    GtkWidget *dialog = gtk_message_dialog_new (parent_window,
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_CLOSE,
                                                "%s", message);

    gtk_window_set_keep_above (GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

static void
init_bindings_from_config (GtkListStore *list_store, GtkTreeIter *iter)
{
    typedef struct keybinding {
        gchar *action;
        gchar *config_name;
    } Keybinding;

    const Keybinding bindings[] =
            {
                    {"Pull Down Terminal",  "key"},
                    {"Quit",                "quit_key"},
                    {"Add Tab",             "addtab_key"},
                    {"Close Tab",           "closetab_key"},
                    {"Copy",                "copy_key"},
                    {"Paste",               "paste_key"},
                    {"Go To Next Tab",      "nexttab_key"},
                    {"Go To Previous Tab",  "prevtab_key"},
                    {"Move Tab Left",       "movetableft_key"},
                    {"Move Tab Right",      "movetabright_key"},
                    {"Toggle Fullscreen",   "fullscreen_key"},
                    {"Toggle Transparency", "toggle_transparency_key"},
                    {"Toggle Searchbar",    "toggle_searchbar_key"},
                    {NULL, NULL}
            };

    const Keybinding *binding = bindings;

    while (binding->action)
    {
        gtk_list_store_append (list_store, iter);

        gchar *shortcut = config_getstr (binding->config_name);

        gtk_list_store_set (list_store, iter,
                            KB_TREE_ACTION, binding->action,
                            KB_TREE_SHORTCUT, shortcut,
                            KB_TREE_CONFIG_NAME, binding->config_name,
                            -1);

        ++binding;
    }

    const Keybinding gototabBindings[] = {
        {"Go To Tab 1",  "gototab_1_key"},
        {"Go To Tab 2",  "gototab_2_key"},
        {"Go To Tab 3",  "gototab_3_key"},
        {"Go To Tab 4",  "gototab_4_key"},
        {"Go To Tab 5",  "gototab_5_key"},
        {"Go To Tab 6",  "gototab_6_key"},
        {"Go To Tab 7",  "gototab_7_key"},
        {"Go To Tab 8",  "gototab_8_key"},
        {"Go To Tab 9",  "gototab_9_key"},
        {"Go To Tab 10", "gototab_10_key"},
        {NULL, NULL}
    };

    binding = gototabBindings;

    while (binding->action)
    {
        gtk_list_store_append (list_store, iter);

        gchar *shortcut = config_getstr (binding->config_name);

        gtk_list_store_set (list_store, iter,
                            KB_TREE_ACTION, binding->action,
                            KB_TREE_SHORTCUT, shortcut,
                            KB_TREE_CONFIG_NAME, binding->config_name,
                            -1);

        ++binding;
    }
}

/*
 * This is called when a shortcut in the keybinding tree view widget
 * is clicked.
 */
static gboolean
keybinding_button_press_event_cb (GtkWidget *tree_view,
                                  GdkEventButton *event,
                                  TildaKeybindingTreeView *keybindings)
{
    GtkBuilder *builder = keybindings->builder;

    const GtkWidget *wizard_window =
            GTK_WIDGET (gtk_builder_get_object (builder, "wizard_window"));

    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {

        GtkTreeIter iter;
        GtkTreeSelection *selection =
                gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
            return GDK_EVENT_PROPAGATE;
        }

        /* Bring up the dialog that will accept the new keybinding */
        GtkWidget *dialog
                = gtk_message_dialog_new (GTK_WINDOW (wizard_window),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_CANCEL,
                                          _ ("Enter keyboard shortcut"));

        /* Connect the key grabber to the dialog */
        g_signal_connect (G_OBJECT (dialog),
                          "key_press_event",
                          G_CALLBACK (keybinding_dialog_key_press_event_cb),
                          keybindings);

        gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
        gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);

        return GDK_EVENT_STOP;
    }

    return GDK_EVENT_PROPAGATE;
}


/*
 * This function gets called once per key that is pressed. This means we have to
 * manually ignore all modifier keys, and only register "real" keys.
 *
 * We return when the first "real" key is pressed.
 *
 * Note that this is called for the key_press_event for the key-grab dialog,
 * not for the keybinding tree view widget.
 */
static gboolean
keybinding_dialog_key_press_event_cb (GtkWidget *dialog,
                                      GdkEventKey *event,
                                      TildaKeybindingTreeView *keybinding)
{
    DEBUG_FUNCTION ("keybinding_dialog_key_press_event_cb");
    DEBUG_ASSERT (keybinding != NULL);
    DEBUG_ASSERT (event != NULL);

    GtkWidget *tree_view = keybinding->tree_view;
    GtkListStore *listStore = keybinding->list_store;

    gchar *key;

    if (gtk_accelerator_valid (event->keyval,
                               (GdkModifierType) event->state))
    {
        GtkTreeIter iter;
        GtkTreeSelection *selection;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));

        if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
            return GDK_EVENT_PROPAGATE;
        }

        /* This lets us ignore all ignorable modifier keys, including
         * NumLock and many others. :)
         *
         * The logic is: keep only the important modifiers that were pressed
         * for this event. */
        event->state &= gtk_accelerator_get_default_mod_mask ();

        /* Generate the correct name for this key */
        key = gtk_accelerator_name (event->keyval,
                                    (GdkModifierType) event->state);

        gtk_list_store_set (listStore, &iter,
                            KB_TREE_SHORTCUT, key, -1);

        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

        g_debug ("KEY GRABBED: %s", key);

        /* Free the string */
        g_free (key);
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean
validate_keybindings (TildaKeybindingTreeView *keybindings,
                      tilda_window *tw)
{
    GtkTreeIter iter;
    gboolean changed;

    GtkListStore *list_store;

    list_store = keybindings->list_store;

    changed = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (list_store),
                                             &iter);

    while (changed) {
        gchar * action, *config_name, *shortcut;

        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
                            KB_TREE_ACTION, &action,
                            KB_TREE_CONFIG_NAME, &config_name,
                            KB_TREE_SHORTCUT, &shortcut,
                            -1);

        if (0 == g_strcmp0 ("key", config_name)) {
            const char *message = _ ("The keybinding you chose for \"Pull Down Terminal\" is invalid. Please choose another.");

            if (!validate_pulldown_keybinding (shortcut, tw,
                                               message))
                return FALSE;
        }
        else {
            gchar * message = g_strdup_printf (
                    _ ("The keybinding you chose for \"%s\" is invalid. Please choose another."),
                    action);

            if (!validate_keybinding (shortcut, tw, message)) {
                g_free (message);
                return FALSE;
            }

        }

        changed = gtk_tree_model_iter_next (GTK_TREE_MODEL (list_store),
                                          &iter);
    }


    return TRUE;
}

static gboolean
validate_pulldown_keybinding (const gchar* accel,
                              tilda_window* tw,
                              const gchar* message)
{
    /* Try to grab the key. This is a good way to validate it :) */
    gboolean key_is_valid = tilda_keygrabber_bind (accel, tw);

    if (key_is_valid)
        return TRUE;
    else
    {
        GtkWindow *window = GTK_WINDOW(tw->wizard_window);

        tilda_keybinding_show_invalid_keybinding_dialog (window, message);

        return FALSE;
    }
}

static gboolean
validate_keybinding (const gchar* accel,
                     tilda_window *tw,
                     const gchar* message)
{
    guint accel_key;
    GdkModifierType accel_mods;

    /* Parse the accelerator string. If it parses improperly, both values will be 0.*/
    gtk_accelerator_parse (accel, &accel_key, &accel_mods);
    if (! ((accel_key == 0) && (accel_mods == 0)) ) {
        return TRUE;
    } else {
        GtkWindow *window = GTK_WINDOW(tw->wizard_window);

        tilda_keybinding_show_invalid_keybinding_dialog (window,
                                                         message);
        return FALSE;
    }
}
