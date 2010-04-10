/* Calf DSP Library Utility Application - calfjackhost
 * A class that contains a JACK host session
 *
 * Copyright (C) 2007-2010 Krzysztof Foltman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <calf/giface.h>
#include <calf/host_session.h>
#include <calf/gui.h>
#include <calf/preset.h>
#include <calf/preset_gui.h>
#include <calf/main_win.h>
#include <getopt.h>

using namespace std;
using namespace calf_utils;
using namespace calf_plugins;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

host_session::host_session()
{
    client_name = "calf";
#if USE_LASH
    lash_source_id = 0;
    lash_client = NULL;
# if !USE_LASH_0_6
    lash_args = NULL;
# endif
#endif
    restoring_session = false;
    main_win = new main_window;
    main_win->set_owner(this);
    progress_window = NULL;
    autoconnect_midi_index = -1;
    gui_win = NULL;
}

std::string host_session::get_next_instance_name(const std::string &effect_name)
{
    if (!instances.count(effect_name))
        return effect_name;
    for (int i = 2; ; i++)
    {
        string tmp = string(effect_name) + i2s(i);
        if (!instances.count(tmp))
            return tmp;
    }
    assert(0);
    return "-";
}

void host_session::add_plugin(string name, string preset, string instance_name)
{
    if (instance_name.empty())
        instance_name = get_next_instance_name(name);
    jack_host *jh = create_jack_host(name.c_str(), instance_name, this);
    if (!jh) {
        string s = 
        #define PER_MODULE_ITEM(name, isSynth, jackname) jackname ", "
        #include <calf/modulelist.h>
        ;
        if (!s.empty())
            s = s.substr(0, s.length() - 2);
        throw text_exception("Unknown plugin name; allowed are: " + s);
    }
    instances.insert(jh->instance_name);
    jh->create(&client);
    
    plugins.push_back(jh);
    client.add(jh);
    main_win->add_plugin(jh);
    if (!preset.empty()) {
        if (!activate_preset(plugins.size() - 1, preset, false))
        {
            if (!activate_preset(plugins.size() - 1, preset, true))
            {
                fprintf(stderr, "Unknown preset: %s\n", preset.c_str());
            }
        }
    }
}

void host_session::report_progress(float percentage, const std::string &message)
{
    if (percentage < 100)
    {
        if (!progress_window) {
            progress_window = create_progress_window();
            gtk_window_set_modal (GTK_WINDOW (progress_window), TRUE);
            if (main_win->toplevel)
                gtk_window_set_transient_for (GTK_WINDOW (progress_window), main_win->toplevel);
        }
        gtk_widget_show(progress_window);
        GtkWidget *pbar = gtk_bin_get_child (GTK_BIN (progress_window));
        if (!message.empty())
            gtk_progress_bar_set_text (GTK_PROGRESS_BAR (pbar), message.c_str());
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pbar), percentage / 100.0);
    }
    else
    {
        if (progress_window) {
            gtk_window_set_modal (GTK_WINDOW (progress_window), FALSE);
            gtk_widget_destroy (progress_window);
            progress_window = NULL;
        }
    }
    
    while (gtk_events_pending ())
        gtk_main_iteration ();
}


void host_session::create_plugins_from_list()
{
    for (unsigned int i = 0; i < plugin_names.size(); i++) {
        add_plugin(plugin_names[i], presets.count(i) ? presets[i] : string());
    }
}

GtkWidget *host_session::create_progress_window()
{
    GtkWidget *tlw = gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_type_hint (GTK_WINDOW (tlw), GDK_WINDOW_TYPE_HINT_DIALOG);
    GtkWidget *pbar = gtk_progress_bar_new();
    gtk_container_add (GTK_CONTAINER(tlw), pbar);
    gtk_widget_show_all (pbar);
    return tlw;
}

static void window_destroy_cb(GtkWindow *window, gpointer data)
{
    gtk_main_quit();
}

void host_session::open()
{
    if (!input_name.empty()) client.input_name = input_name;
    if (!output_name.empty()) client.output_name = output_name;
    if (!midi_name.empty()) client.midi_name = midi_name;
    
    client.open(client_name.c_str());
    main_win->prefix = client_name + " - ";
    main_win->conditions.insert("jackhost");
    main_win->conditions.insert("directlink");
    if (!restoring_session)
        create_plugins_from_list();
    main_win->create();
    gtk_signal_connect(GTK_OBJECT(main_win->toplevel), "destroy", G_CALLBACK(window_destroy_cb), NULL);
}

void host_session::new_plugin(const char *name)
{
    jack_host *jh = create_jack_host(name, get_next_instance_name(name), this);
    if (!jh)
        return;
    instances.insert(jh->instance_name);
    jh->create(&client);
    
    plugins.push_back(jh);
    client.add(jh);
    main_win->add_plugin(jh);
}

void host_session::remove_plugin(plugin_ctl_iface *plugin)
{
    for (unsigned int i = 0; i < plugins.size(); i++)
    {
        if (plugins[i] == plugin)
        {
            client.del(i);
            plugins.erase(plugins.begin() + i);
            main_win->del_plugin(plugin);
            delete plugin;
            return;
        }
    }
}

void host_session::remove_all_plugins()
{
    while(!plugins.empty())
    {
        plugin_ctl_iface *plugin = plugins[0];
        client.del(0);
        plugins.erase(plugins.begin());
        main_win->del_plugin(plugin);
        delete plugin;
    }
}

bool host_session::activate_preset(int plugin_no, const std::string &preset, bool builtin)
{
    string cur_plugin = plugins[plugin_no]->metadata->get_id();
    preset_vector &pvec = (builtin ? get_builtin_presets() : get_user_presets()).presets;
    for (unsigned int i = 0; i < pvec.size(); i++) {
        if (pvec[i].name == preset && pvec[i].plugin == cur_plugin)
        {
            pvec[i].activate(plugins[plugin_no]);
            if (gui_win && gui_win->gui)
                gui_win->gui->refresh();
            return true;
        }
    }
    return false;
}

void host_session::connect()
{
    client.activate();
#if USE_LASH && !USE_LASH_0_6
    if (lash_client)
        lash_jack_client_name(lash_client, client.get_name().c_str());
#endif
    if (!restoring_session) 
    {
        string cnp = client.get_name() + ":";
        for (unsigned int i = 0; i < plugins.size(); i++) {
            if (chains.count(i)) {
                if (!i)
                {
                    if (plugins[0]->metadata->get_output_count() < 2)
                    {
                        fprintf(stderr, "Cannot connect input to plugin %s - incompatible ports\n", plugins[0]->name.c_str());
                    } else {
                        client.connect("system:capture_1", cnp + plugins[0]->get_inputs()[0].name);
                        client.connect("system:capture_2", cnp + plugins[0]->get_inputs()[1].name);
                    }
                }
                else
                {
                    if (plugins[i - 1]->metadata->get_output_count() < 2 || plugins[i]->metadata->get_input_count() < 2)
                    {
                        fprintf(stderr, "Cannot connect plugins %s and %s - incompatible ports\n", plugins[i - 1]->name.c_str(), plugins[i]->name.c_str());
                    }
                    else {
                        client.connect(cnp + plugins[i - 1]->get_outputs()[0].name, cnp + plugins[i]->get_inputs()[0].name);
                        client.connect(cnp + plugins[i - 1]->get_outputs()[1].name, cnp + plugins[i]->get_inputs()[1].name);
                    }
                }
            }
        }
        if (chains.count(plugins.size()) && plugins.size())
        {
            int last = plugins.size() - 1;
            if (plugins[last]->metadata->get_output_count() < 2)
            {
                fprintf(stderr, "Cannot connect plugin %s to output - incompatible ports\n", plugins[last]->name.c_str());
            } else {
                client.connect(cnp + plugins[last]->get_outputs()[0].name, "system:playback_1");
                client.connect(cnp + plugins[last]->get_outputs()[1].name, "system:playback_2");
            }
        }
        if (autoconnect_midi != "") {
            for (unsigned int i = 0; i < plugins.size(); i++)
            {
                if (plugins[i]->metadata->get_midi())
                    client.connect(autoconnect_midi, cnp + plugins[i]->get_midi_port()->name);
            }
        }
        else
        if (autoconnect_midi_index != -1) {
            const char **ports = client.get_ports("(system|alsa_pcm):.*", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
            for (int j = 0; ports && ports[j]; j++)
            {
                if (j + 1 == autoconnect_midi_index) {
                    for (unsigned int i = 0; i < plugins.size(); i++)
                    {
                        if (plugins[i]->metadata->get_midi())
                            client.connect(ports[j], cnp + plugins[i]->get_midi_port()->name);
                    }
                    break;
                }
            }
        }
    }
#if USE_LASH
    if (lash_client)
    {
# if !USE_LASH_0_6
        send_lash(LASH_Client_Name, "calf-"+client_name);
# endif
        lash_source_id = g_timeout_add_full(G_PRIORITY_LOW, 250, update_lash, this, NULL); // 4 LASH reads per second... should be enough?
    }
#endif
}

void host_session::close()
{
#if USE_LASH
    if (lash_source_id)
        g_source_remove(lash_source_id);
#endif
    main_win->on_closed();
    main_win->close_guis();
    client.deactivate();
    client.delete_plugins();
    client.close();
#if USE_LASH && !USE_LASH_0_6
    if (lash_args)
        lash_args_destroy(lash_args);
#endif
}

static string stripfmt(string x)
{
    if (x.length() < 2)
        return x;
    if (x.substr(x.length() - 2) != "%d")
        return x;
    return x.substr(0, x.length() - 2);
}

char *host_session::open_file(const char *name)
{
    preset_list pl;
    try {
        remove_all_plugins();
        pl.load(name, true);
        printf("Size %d\n", (int)pl.plugins.size());
        for (unsigned int i = 0; i < pl.plugins.size(); i++)
        {
            preset_list::plugin_snapshot &ps = pl.plugins[i];
            client.input_nr = ps.input_index;
            client.output_nr = ps.output_index;
            client.midi_nr = ps.midi_index;
            printf("Loading %s\n", ps.type.c_str());
            if (ps.preset_offset < (int)pl.presets.size())
            {
                add_plugin(ps.type, "", ps.instance_name);
                pl.presets[ps.preset_offset].activate(plugins[i]);
                main_win->refresh_plugin(plugins[i]);
            }
        }
    }
    catch(preset_exception &e)
    {
        // XXXKF this will leak
        char *data = strdup(e.what());
        return data;
    }
    
    return NULL;
}

char *host_session::save_file(const char *name)
{
    string i_name = stripfmt(client.input_name);
    string o_name = stripfmt(client.output_name);
    string m_name = stripfmt(client.midi_name);
    string data;
    data = "<?xml version=\"1.1\" encoding=\"utf-8\">\n";
    data = "<rack>\n";
    for (unsigned int i = 0; i < plugins.size(); i++) {
        jack_host *p = plugins[i];
        plugin_preset preset;
        preset.plugin = p->metadata->get_id();
        preset.get_from(p);
        data += "<plugin";
        data += to_xml_attr("type", preset.plugin);
        data += to_xml_attr("instance-name", p->instance_name);
        if (p->metadata->get_input_count())
            data += to_xml_attr("input-index", p->get_inputs()[0].name.substr(i_name.length()));
        if (p->metadata->get_output_count())
            data += to_xml_attr("output-index", p->get_outputs()[0].name.substr(o_name.length()));
        if (p->get_midi_port())
            data += to_xml_attr("midi-index", p->get_midi_port()->name.substr(m_name.length()));
        data += ">\n";
        data += preset.to_xml();
        data += "</plugin>\n";
    }
    data += "</rack>\n";
    FILE *f = fopen(name, "w");
    if (!f || 1 != fwrite(data.c_str(), data.length(), 1, f))
    {
        int e = errno;
        fclose(f);
        return strdup(strerror(e));
    }
    if (fclose(f))
        return strdup(strerror(errno));
    
    return NULL;
}

#if USE_LASH

# if !USE_LASH_0_6

void host_session::update_lash()
{
    do {
        lash_event_t *event = lash_get_event(lash_client);
        if (!event)
            break;
        
        // printf("type = %d\n", lash_event_get_type(event));
        
        switch(lash_event_get_type(event)) {        
            case LASH_Save_Data_Set:
            {
                lash_config_t *cfg = lash_config_new_with_key("global");
                dictionary tmp;
                string pstr;
                string i_name = stripfmt(client.input_name);
                string o_name = stripfmt(client.output_name);
                string m_name = stripfmt(client.midi_name);
                tmp["input_prefix"] = i_name;
                tmp["output_prefix"] = stripfmt(client.output_name);
                tmp["midi_prefix"] = stripfmt(client.midi_name);
                pstr = encode_map(tmp);
                lash_config_set_value(cfg, pstr.c_str(), pstr.length());
                lash_send_config(lash_client, cfg);
                
                for (unsigned int i = 0; i < plugins.size(); i++) {
                    jack_host *p = plugins[i];
                    char ss[32];
                    plugin_preset preset;
                    preset.plugin = p->metadata->get_id();
                    preset.get_from(p);
                    sprintf(ss, "Plugin%d", i);
                    pstr = preset.to_xml();
                    tmp.clear();
                    tmp["instance_name"] = p->instance_name;
                    if (p->metadata->get_input_count())
                        tmp["input_name"] = p->get_inputs()[0].name.substr(i_name.length());
                    if (p->metadata->get_output_count())
                        tmp["output_name"] = p->get_outputs()[0].name.substr(o_name.length());
                    if (p->get_midi_port())
                        tmp["midi_name"] = p->get_midi_port()->name.substr(m_name.length());
                    tmp["preset"] = pstr;
                    pstr = encode_map(tmp);
                    lash_config_t *cfg = lash_config_new_with_key(ss);
                    lash_config_set_value(cfg, pstr.c_str(), pstr.length());
                    lash_send_config(lash_client, cfg);
                }
                send_lash(LASH_Save_Data_Set, "");
                break;
            }
            
            case LASH_Restore_Data_Set:
            {
                // printf("!!!Restore data set!!!\n");
                remove_all_plugins();
                while(lash_config_t *cfg = lash_get_config(lash_client)) {
                    const char *key = lash_config_get_key(cfg);
                    // printf("key = %s\n", lash_config_get_key(cfg));
                    string data = string((const char *)lash_config_get_value(cfg), lash_config_get_value_size(cfg));
                    if (!strcmp(key, "global"))
                    {
                        dictionary dict;
                        decode_map(dict, data);
                        if (dict.count("input_prefix")) client.input_name = dict["input_prefix"]+"%d";
                        if (dict.count("output_prefix")) client.output_name = dict["output_prefix"]+"%d";
                        if (dict.count("midi_prefix")) client.midi_name = dict["midi_prefix"]+"%d";
                    }
                    if (!strncmp(key, "Plugin", 6))
                    {
                        unsigned int nplugin = atoi(key + 6);
                        dictionary dict;
                        decode_map(dict, data);
                        data = dict["preset"];
                        string instance_name;
                        if (dict.count("instance_name")) instance_name = dict["instance_name"];
                        if (dict.count("input_name")) client.input_nr = atoi(dict["input_name"].c_str());
                        if (dict.count("output_name")) client.output_nr = atoi(dict["output_name"].c_str());
                        if (dict.count("midi_name")) client.midi_nr = atoi(dict["midi_name"].c_str());
                        preset_list tmp;
                        tmp.parse("<presets>"+data+"</presets>", false);
                        if (tmp.presets.size())
                        {
                            printf("Load plugin %s\n", tmp.presets[0].plugin.c_str());
                            add_plugin(tmp.presets[0].plugin, "", instance_name);
                            tmp.presets[0].activate(plugins[nplugin]);
                            main_win->refresh_plugin(plugins[nplugin]);
                        }
                    }
                    lash_config_destroy(cfg);
                }
                send_lash(LASH_Restore_Data_Set, "");
                break;
            }
                
            case LASH_Quit:
                gtk_main_quit();
                break;
            
            default:
                g_warning("Unhandled LASH event %d (%s)", lash_event_get_type(event), lash_event_get_string(event));
                break;
        }
    } while(1);
}

# else

void host_session::update_lash()
{
    lash_dispatch(lash_client);
}

static bool save_data_set_cb(lash_config_handle_t *handle, void *user_data)
{
    host_session *sess = static_cast<host_session *>(user_data);
    dictionary tmp;
    string pstr;
    string i_name = stripfmt(sess->client.input_name);
    string o_name = stripfmt(sess->client.output_name);
    string m_name = stripfmt(sess->client.midi_name);
    tmp["input_prefix"] = i_name;
    tmp["output_prefix"] = stripfmt(sess->client.output_name);
    tmp["midi_prefix"] = stripfmt(sess->client.midi_name);
    pstr = encode_map(tmp);
    lash_config_write_raw(handle, "global", pstr.c_str(), pstr.length());
    for (unsigned int i = 0; i < sess->plugins.size(); i++) {
        jack_host *p = sess->plugins[i];
        char ss[32];
        plugin_preset preset;
        preset.plugin = p->metadata->get_id();
        preset.get_from(p);
        sprintf(ss, "Plugin%d", i);
        pstr = preset.to_xml();
        tmp.clear();
        tmp["instance_name"] = p->instance_name;
        if (p->metadata->get_input_count())
            tmp["input_name"] = p->get_inputs()[0].name.substr(i_name.length());
        if (p->metadata->get_output_count())
            tmp["output_name"] = p->get_outputs()[0].name.substr(o_name.length());
        if (p->get_midi_port())
            tmp["midi_name"] = p->get_midi_port()->name.substr(m_name.length());
        tmp["preset"] = pstr;
        pstr = encode_map(tmp);
        lash_config_write_raw(handle, ss, pstr.c_str(), pstr.length());
    }
    return true;
}

static bool load_data_set_cb(lash_config_handle_t *handle, void *user_data)
{
    host_session *sess = static_cast<host_session *>(user_data);
    int size, type;
    const char *key;
    const char *value;
    sess->remove_all_plugins();
    while((size = lash_config_read(handle, &key, (void *)&value, &type))) {
        if (size == -1 || type != LASH_TYPE_RAW)
            continue;
        string data = string(value, size);
        if (!strcmp(key, "global"))
        {
            dictionary dict;
            decode_map(dict, data);
            if (dict.count("input_prefix")) sess->client.input_name = dict["input_prefix"]+"%d";
            if (dict.count("output_prefix")) sess->client.output_name = dict["output_prefix"]+"%d";
            if (dict.count("midi_prefix")) sess->client.midi_name = dict["midi_prefix"]+"%d";
        } else if (!strncmp(key, "Plugin", 6)) {
            unsigned int nplugin = atoi(key + 6);
            dictionary dict;
            decode_map(dict, data);
            data = dict["preset"];
            string instance_name;
            if (dict.count("instance_name")) instance_name = dict["instance_name"];
            if (dict.count("input_name")) sess->client.input_nr = atoi(dict["input_name"].c_str());
            if (dict.count("output_name")) sess->client.output_nr = atoi(dict["output_name"].c_str());
            if (dict.count("midi_name")) sess->client.midi_nr = atoi(dict["midi_name"].c_str());
            preset_list tmp;
            tmp.parse("<presets>"+data+"</presets>", false);
            if (tmp.presets.size())
            {
                printf("Load plugin %s\n", tmp.presets[0].plugin.c_str());
                sess->add_plugin(tmp.presets[0].plugin, "", instance_name);
                tmp.presets[0].activate(sess->plugins[nplugin]);
                sess->main_win->refresh_plugin(sess->plugins[nplugin]);
            }
        }
    }
    return true;
}

static bool quit_cb(void *user_data)
{
    gtk_main_quit();
    return true;
}

# endif
#endif

void host_session::connect_to_session_manager(int argc, char *argv[])
{
#if USE_LASH
# if !USE_LASH_0_6
    for (int i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "--lash-project=", 14)) {
            restoring_session = true;
            break;
        }
    }
    lash_args = lash_extract_args(&argc, &argv);
    lash_client = lash_init(lash_args, PACKAGE_NAME, LASH_Config_Data_Set, LASH_PROTOCOL(2, 0));
# else
    lash_client = lash_client_open(PACKAGE_NAME, LASH_Config_Data_Set, argc, argv);
    restoring_session = lash_client_is_being_restored(lash_client);
    lash_set_save_data_set_callback(lash_client, save_data_set_cb, this);
    lash_set_load_data_set_callback(lash_client, load_data_set_cb, this);
    lash_set_quit_callback(lash_client, quit_cb, NULL);
# endif
    if (!lash_client) {
        g_warning("Warning: failed to create a LASH connection");
    }
#endif
}
