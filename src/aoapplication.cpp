#include "aoapplication.h"

#include "courtroom.h"
#include "debug_functions.h"
#include "lobby.h"
#include "networkmanager.h"
#include "options.h"
#include "widgets/aooptionsdialog.h"

static QtMessageHandler original_message_handler;
static AOApplication *message_handler_context;

void message_handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
  Q_EMIT message_handler_context->qt_log_message(type, context, msg);
  original_message_handler(type, context, msg);
}

AOApplication::AOApplication(QObject *parent)
    : QObject(parent)
{
  net_manager = new NetworkManager(this);
  discord = new AttorneyOnline::Discord();

  asset_lookup_cache.reserve(2048);

  message_handler_context = this;
  original_message_handler = qInstallMessageHandler(message_handler);
}

AOApplication::~AOApplication()
{
  destruct_lobby();
  destruct_courtroom();
  delete discord;
  qInstallMessageHandler(original_message_handler);
}

bool AOApplication::is_lobby_constructed()
{
  return w_lobby;
}

void AOApplication::construct_lobby()
{
  if (is_lobby_constructed())
  {
    qWarning() << "lobby was attempted constructed when it already exists";
    return;
  }

  w_lobby = new Lobby(this, net_manager);

  centerOrMoveWidgetOnPrimaryScreen(w_lobby);

  if (Options::getInstance().discordEnabled())
  {
    discord->state_lobby();
  }

  if (demo_server)
  {
    demo_server->deleteLater();
  }
  demo_server = new DemoServer(this);
  w_lobby->show();
}

void AOApplication::destruct_lobby()
{
  if (!is_lobby_constructed())
  {
    qWarning() << "lobby was attempted destructed when it did not exist";
    return;
  }

  delete w_lobby;
  w_lobby = nullptr;
}

bool AOApplication::is_courtroom_constructed()
{
  return w_courtroom;
}

void AOApplication::construct_courtroom()
{
  if (is_courtroom_constructed())
  {
    qWarning() << "courtroom was attempted constructed when it already exists";
    return;
  }

  w_courtroom = new Courtroom(this);

  centerOrMoveWidgetOnPrimaryScreen(w_courtroom);

  if (demo_server != nullptr)
  {
    QObject::connect(demo_server, &DemoServer::skip_timers, w_courtroom, &Courtroom::skip_clocks);
  }
  else
  {
    qWarning() << "demo server did not exist during courtroom construction";
  }
}

void AOApplication::destruct_courtroom()
{
  if (!is_courtroom_constructed())
  {
    qWarning() << "courtroom was attempted destructed when it did not exist";
    return;
  }

  delete w_courtroom;
  w_courtroom = nullptr;
}

QString AOApplication::get_version_string()
{
  return QString::number(RELEASE) + "." + QString::number(MAJOR_VERSION) + "." + QString::number(MINOR_VERSION) + " RC3";
}

QString AOApplication::find_image(QStringList p_list)
{
  QString image_path;
  for (const QString &path : p_list)
  {
    if (file_exists(path))
    {
      image_path = path;
      break;
    }
  }
  return image_path;
}

void AOApplication::server_disconnected()
{
  if (is_courtroom_constructed())
  {
    if (w_courtroom->isVisible())
    {
      call_notice(tr("Disconnected from server."));
    }
    construct_lobby();
    destruct_courtroom();
  }
  Options::getInstance().setServerSubTheme(QString());
}

void AOApplication::loading_cancelled()
{
  destruct_courtroom();
}

void AOApplication::call_settings_menu()
{
  AOOptionsDialog *l_dialog = new AOOptionsDialog(this);
  if (is_courtroom_constructed())
  {
    connect(l_dialog, &AOOptionsDialog::reloadThemeRequest, w_courtroom, &Courtroom::on_reload_theme_clicked);
  }

  if (is_lobby_constructed())
  {}
  l_dialog->exec();
  delete l_dialog;
}

// Callback for when BASS device is lost
// Only actually used for music syncs
void CALLBACK AOApplication::BASSreset(HSTREAM handle, DWORD channel, DWORD data, void *user)
{
  Q_UNUSED(handle);
  Q_UNUSED(channel);
  Q_UNUSED(data);
  Q_UNUSED(user);
  doBASSreset();
}

void AOApplication::doBASSreset()
{
  BASS_Free();
  BASS_Init(-1, 48000, BASS_DEVICE_LATENCY, nullptr, nullptr);
  load_bass_plugins();
}

void AOApplication::server_connected()
{
  qInfo() << "Established connection to server.";

  destruct_courtroom();
  construct_courtroom();

  courtroom_loaded = false;
}

void AOApplication::initBASS()
{
  BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, 1);
  BASS_Free();
  // Change the default audio output device to be the one the user has given
  // in his config.ini file for now.
  unsigned int a = 0;
  BASS_DEVICEINFO info;

  if (Options::getInstance().audioOutputDevice() == "default")
  {
    BASS_Init(-1, 48000, BASS_DEVICE_LATENCY, nullptr, nullptr);
    load_bass_plugins();
  }
  else
  {
    for (a = 0; BASS_GetDeviceInfo(a, &info); a++)
    {
      if (Options::getInstance().audioOutputDevice() == info.name)
      {
        BASS_SetDevice(a);
        BASS_Init(static_cast<int>(a), 48000, BASS_DEVICE_LATENCY, nullptr, nullptr);
        load_bass_plugins();
        qInfo() << info.name << "was set as the default audio output device.";
        return;
      }
    }
    BASS_Init(-1, 48000, BASS_DEVICE_LATENCY, nullptr, nullptr);
    load_bass_plugins();
  }
}

bool AOApplication::pointExistsOnScreen(QPoint point)
{
  for (QScreen *screen : QApplication::screens())
  {
    if (screen->availableGeometry().contains(point))
    {
      return true;
    }
  }
  return false;
}

void AOApplication::centerOrMoveWidgetOnPrimaryScreen(QWidget *widget)
{
  auto point = Options::getInstance().windowPosition(widget->objectName());
  if (!Options::getInstance().restoreWindowPositionEnabled() || !point.has_value() || !pointExistsOnScreen(point.value()))
  {
    QRect geometry = QGuiApplication::primaryScreen()->geometry();
    int x = (geometry.width() - widget->width()) / 2;
    int y = (geometry.height() - widget->height()) / 2;
    widget->move(x, y);
  }
  else
  {
    widget->move(point->x(), point->y());
  }
}

#if (defined(_WIN32) || defined(_WIN64))
void AOApplication::load_bass_plugins()
{
  BASS_PluginLoad("bassopus.dll", 0);
}
#elif defined __APPLE__
void AOApplication::load_bass_plugins()
{
  BASS_PluginLoad("libbassopus.dylib", 0);
}
#elif (defined(LINUX) || defined(__linux__))
void AOApplication::load_bass_plugins()
{
  BASS_PluginLoad("libbassopus.so", 0);
}
#else
#error This operating system is unsupported for BASS plugins.
#endif
