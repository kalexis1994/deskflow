/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include "base/Stopwatch.h"
#include "common/NetworkProtocol.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/ClipboardUtil.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/Monitors.h"
#include "deskflow/MouseTypes.h"
#include "server/Config.h"

#include <climits>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class BaseClientProxy;
class EventQueueTimer;
class PrimaryClient;
class InputFilter;
namespace deskflow {
class Computer;
}
class IEventQueue;
class Thread;
namespace deskflow::files {
class FileChannel;
class FileHub;
class FileProgress;
struct FileList;
} // namespace deskflow::files
class ClientListener;

//! Deskflow server
/*!
This class implements the top-level server algorithms for deskflow.
*/
class Server
{
  using ServerConfig = deskflow::server::Config;

public:
  //! Lock cursor to computer data
  class LockCursorToComputerInfo : public EventData
  {
  public:
    enum State
    {
      kOff,
      kOn,
      kToggle
    };

    explicit LockCursorToComputerInfo(State state = kToggle) : m_state(state)
    {
      // do nothing
    }
    ~LockCursorToComputerInfo() override = default; // do nothing

  public:
    State m_state;
  };

  //! Switch to computer data
  class SwitchToComputerInfo : public EventData
  {
  public:
    explicit SwitchToComputerInfo(const std::string &computer) : m_computer(computer)
    {
      // do nothing
    }
    ~SwitchToComputerInfo() override = default; // do nothing

  public:
    std::string m_computer;
  };

  //! Switch in direction data
  class SwitchInDirectionInfo : public EventData
  {
  public:
    explicit SwitchInDirectionInfo(Direction direction) : m_direction(direction)
    {
      // do nothing
    }
    ~SwitchInDirectionInfo() override = default; // do nothing

  public:
    Direction m_direction;
  };

  //! Computer connected data
  class ComputerConnectedInfo
  {
  public:
    explicit ComputerConnectedInfo(std::string computer) : m_computer(computer)
    {
      // do nothing
    }

  public:
    std::string m_computer; // was char[1]
  };

  //! Keyboard broadcast data
  class KeyboardBroadcastInfo : public EventData
  {
  public:
    enum State
    {
      kOff,
      kOn,
      kToggle
    };

    explicit KeyboardBroadcastInfo(State state = kToggle) : m_state(state)
    {
      // do nothing
    }
    KeyboardBroadcastInfo(State state, const std::string &computers) : m_state(state), m_computers(computers)
    {
      // do nothing
    }
    ~KeyboardBroadcastInfo() override = default; // do nothing

  public:
    State m_state;
    std::string m_computers;
  };

  /*!
  Start the server with the configuration \p config and the primary
  client (local computer) \p primaryClient.  The client retains
  ownership of \p primaryClient.
  */
  Server(ServerConfig &config, PrimaryClient *primaryClient, deskflow::Computer *computer, IEventQueue *events);
  Server(Server const &) = delete;
  Server(Server &&) = delete;
  ~Server();

  Server &operator=(Server const &) = delete;
  Server &operator=(Server &&) = delete;

  //! @name manipulators
  //@{

  //! Set configuration
  /*!
  Change the server's configuration.  Returns true iff the new
  configuration was accepted (it must include the server's name).
  This will disconnect any clients no longer in the configuration.
  */
  bool setConfig(const ServerConfig &);

  //! Add a client
  /*!
  Adds \p client to the server.  The client is adopted and will be
  destroyed when the client disconnects or is disconnected.
  */
  void adoptClient(BaseClientProxy *client);

  //! Disconnect clients
  /*!
  Disconnect clients.  This tells them to disconnect but does not wait
  for them to actually do so.  The server sends the disconnected event
  when they're all disconnected (or immediately if none are connected).
  The caller can also just destroy this object to force the disconnection.
  */
  void disconnect();

  //! Store ClientListener pointer
  void setListener(ClientListener *p)
  {
    m_clientListener = p;
  }

  //@}
  //! @name accessors
  //@{

  //! Get the network protocol
  /*!
  Returns the network protocol used by the server.
  */
  std::string protocolString() const;

  //! Get number of connected clients
  /*!
  Returns the number of connected clients, including the server itself.
  */
  uint32_t getNumClients() const;

  //! Get the list of connected clients
  /*!
  Set the \c list to the names of the currently connected clients.
  */
  void getClients(std::vector<std::string> &list) const;

  //! A program on \p client took the mouse (a first-person game), or let go of it
  void mouseCaptured(BaseClientProxy *client, bool captured);

  //! A client's monitors, for the screen layout
  void clientMonitors(BaseClientProxy *client, std::vector<deskflow::Monitor> monitors);

  //! The computers a key goes to
  /*!
  Keys a hotkey sends name the computers it is for (`computers`, or "*" for any). They
  go only where the mouse is (`active`), and only if that computer is one of them, so a
  hotkey never acts on a computer nobody is looking at. Typed keys go where the mouse
  is, or to the computers keyboard broadcasting names.
  */
  //! Whether going to \p to now would undo the switch that just happened
  /*!
  The cursor arrives at a computer right on its edge, and the very next motion, from
  before the switch or a twitch of the hand, can take it back over that edge within a
  few milliseconds; it shows as the cursor flicking back to the other computer. Nobody
  turns around that fast on purpose, so going back to the computer just left within
  \p guardMs of arriving isn't a switch.
  */
  static bool isBounce(const void *to, const void *cameFrom, double sinceArrivalMs, double guardMs = kBounceGuardMs);

  //! See isBounce()
  static constexpr double kBounceGuardMs = 100.0;

  static std::vector<std::string> keyRecipients(
      const char *computers, const std::string &active, bool broadcasting, const std::string &broadcastComputers,
      const std::vector<std::string> &clients
  );
  void sendConnectedClientsIpc() const;
  size_t getMaximumClipboardSizeBytes() const;

  //@}

  //! @name copying files (docs/dev/file-clipboard.md)
  //@{
  //! The largest copy accepted; 0 when copying files is off
  uint64_t fileClipboardMaxBytes() const
  {
    return m_fileHub ? m_fileMaxBytes : 0;
  }
  //! A client offers its copy: the reason to refuse it, or nothing
  std::optional<std::string>
  acceptFiles(BaseClientProxy *from, const std::string &id, const deskflow::files::FileList &list);
  void filesReceived(BaseClientProxy *from, const std::string &id, const std::vector<std::filesystem::path> &roots);
  void filesReceiveFailed(BaseClientProxy *from, const std::string &id, const std::string &reason);
  void filesSent(BaseClientProxy *to, const std::string &id);
  void filesSendFailed(BaseClientProxy *to, const std::string &id, const std::string &reason);
  void filesProgress(BaseClientProxy *client, bool sending, const deskflow::files::FileList &list, uint64_t done);
  //@}

private:
  // get canonical name of client
  std::string getName(const BaseClientProxy *) const;

  // get the sides of the primary computer that have neighbors
  uint32_t getActivePrimarySides() const;

  // returns true iff mouse should be locked to the current computer
  // according to this object only, ignoring what the primary client
  // says.
  bool isLockedToComputerServer() const;

  // returns true iff mouse should be locked to the current m_computer
  // according to this object or the primary client.
  bool isLockedToComputer() const;

  // returns the jump zone of the client
  int32_t getJumpZoneSize(const BaseClientProxy *) const;

  // change the active m_computer
  void switchComputer(BaseClientProxy *, int32_t x, int32_t y, bool forScreenSaver);

  // jump to computer
  void jumpToComputer(BaseClientProxy *);

  // convert pixel position to fraction, using x or y depending on the
  // direction.
  float mapToFraction(const BaseClientProxy *, Direction, int32_t x, int32_t y) const;

  // convert fraction to pixel position, writing only x or y depending
  // on the direction.
  void mapToPixel(const BaseClientProxy *, Direction, float f, int32_t &x, int32_t &y) const;

  // returns true if the client has a neighbor anywhere along the edge
  // indicated by the direction.
  bool hasAnyNeighbor(const BaseClientProxy *, Direction) const;

  // lookup neighboring computer, mapping the coordinate independent of
  // the direction to the neighbor's coordinate space.
  BaseClientProxy *getNeighbor(const BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // lookup neighboring computer.  given a position relative to the
  // source computer, find the computer we should move onto and where.
  // if the position is sufficiently far from the source then we
  // cross multiple computers.  if there is no suitable computer then
  // return nullptr and x,y are not modified.
  BaseClientProxy *mapToNeighbor(BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // adjusts x and y or neither to avoid ending up in a jump zone
  // after entering the client in the given direction.
  void avoidJumpZone(const BaseClientProxy *, Direction, int32_t &x, int32_t &y) const;

  // test if a switch is permitted.  this includes testing user
  // options like switch delay and tracking any state required to
  // implement them.  returns true iff a switch is permitted.
  bool isSwitchOkay(BaseClientProxy *dst, Direction, int32_t x, int32_t y, int32_t xActive, int32_t yActive);

  // update switch state due to a mouse move at \p x, \p y that
  // doesn't switch computers.
  void noSwitch(int32_t x, int32_t y);

  // stop switch timers
  void stopSwitch();

  // start two tap switch timer
  void startSwitchTwoTap();

  // arm the two tap switch timer if \p x, \p y is outside the tap zone
  void armSwitchTwoTap(int32_t x, int32_t y);

  // stop the two tap switch timer
  void stopSwitchTwoTap();

  // returns true iff the two tap switch timer is started
  bool isSwitchTwoTapStarted() const;

  // returns true iff should switch because of two tap
  bool shouldSwitchTwoTap() const;

  // start delay switch timer
  void startSwitchWait(int32_t x, int32_t y);

  // stop delay switch timer
  void stopSwitchWait();

  // returns true iff the delay switch timer is started
  bool isSwitchWaitStarted() const;

  // returns the corner (ScreenSwitchCornerMasks) where x,y is on the
  // given client.  corners have the given size.
  uint32_t getCorner(const BaseClientProxy *, int32_t x, int32_t y, int32_t size) const;

  // stop relative mouse moves
  void stopRelativeMoves();

  // send computer options to \c client
  void sendOptions(BaseClientProxy *client) const;

  // process options from configuration
  void processOptions();

  // event handlers
  void handleShapeChanged(BaseClientProxy *client);
  void handleClipboardGrabbed(const Event &event, BaseClientProxy *client);
  void handleClipboardChanged(const Event &event, BaseClientProxy *client);
  void handleKeyDownEvent(const Event &event);
  void handleKeyUpEvent(const Event &event);
  void handleKeyRepeatEvent(const Event &event);
  void handleButtonDownEvent(const Event &event);
  void handleButtonUpEvent(const Event &event);
  void handleMotionPrimaryEvent(const Event &event);
  void handleMotionSecondaryEvent(const Event &event);
  void handleWheelEvent(const Event &event);
  void handleSwitchWaitTimeout();
  void handleClientDisconnected(BaseClientProxy *client);
  void handleClientCloseTimeout(BaseClientProxy *client);
  void handleSwitchToComputerEvent(const Event &event);
  void handleSwitchInDirectionEvent(const Event &event);
  void handleToggleComputerEvent(const Event &);
  void handleKeyboardBroadcastEvent(const Event &event);
  void handleLockCursorToComputerEvent(const Event &event);

  // event processing
  void onClipboardChanged(const BaseClientProxy *sender, ClipboardID id, uint32_t seqNum);
  void scheduleClipboardRetry();
  void handleClipboardRetry();
  void cleanupClipboardRetry();
  //! Tells the GUI about clipboard activity (only the clipboard, not the X11 selection)
  void reportClipboard(ClipboardID id, const deskflow::clipboard::Activity &activity) const;
  //! Tells the hub what files the server's clipboard has, after a copy here; whether it has any
  bool readLocalFiles();
  deskflow::files::FileChannel *fileChannel(const std::string &computer) const;
  void onScreensaver(bool activated);
  void onKeyDown(KeyID, KeyModifierMask, KeyButton, const std::string &, const char *computers);
  void onKeyUp(KeyID, KeyModifierMask, KeyButton, const char *computers);

  //! Whether a game on the active computer has the mouse: then the cursor stays there
  //! and moves as relative motion, which is all such a game reads
  bool isGameMode() const;
  //! Puts a client's cursor back where the server thinks it is, after relative motion
  void syncAbsolutePosition();
  //! Tells the app which computer is in game mode, or none
  void reportGameMode() const;
  //! Tells the app every computer's monitors, this one's included
  void reportMonitors() const;
  std::vector<BaseClientProxy *> keyClients(const char *computers) const;
  void onKeyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &);
  void onMouseDown(ButtonID);
  void onMouseUp(ButtonID);
  bool onMouseMovePrimary(int32_t x, int32_t y);
  void onMouseMoveSecondary(int32_t dx, int32_t dy);
  void onRawMouseMoveSecondary(int32_t dx, int32_t dy);
  //! The primary can't measure the mouse while on a client: the cursor comes back to it
  void handleCursorLost();
  //! Whether the active client gets relative motion: a game has the mouse, or the
  //! cursor is locked there with relative moves on
  bool isRelativeNow() const;

  void onMouseWheel(int32_t xDelta, int32_t yDelta);

  // add client to list and attach event handlers for client
  bool addClient(BaseClientProxy *);

  // remove client from list and detach event handlers for client
  bool removeClient(BaseClientProxy *);

  // close a client
  void closeClient(BaseClientProxy *, const char *msg);

  // close clients not in \p config
  void closeClients(const ServerConfig &config);

  // close all clients whether they've completed the handshake or not,
  // except the primary client
  void closeAllClients();

  // remove clients from internal state
  void removeActiveClient(BaseClientProxy *);
  void removeOldClient(BaseClientProxy *);

  // force the cursor off of \p client
  void forceLeaveClient(const BaseClientProxy *client);

private:
  class ClipboardInfo
  {
  public:
    ClipboardInfo() = default;

  public:
    Clipboard m_clipboard;
    std::string m_clipboardData;
    std::string m_clipboardOwner;
    uint32_t m_clipboardSeqNum = 0;
    //! The owner copied but its data hasn't arrived: don't hand out the old content as if new
    bool m_pending = false;
    //! Reading the primary computer's clipboard failed and is retried
    bool m_readPending = false;
  };

  //! Drops the data of a clipboard whose last copy can't be shared as such
  static void forgetClipboard(ClipboardInfo &clipboard);
  // Order suggested by clang

  // the Primary Computer Client
  PrimaryClient *m_primaryClient = nullptr;

  // the client with focus
  BaseClientProxy *m_active = nullptr;

  // current configuration
  ServerConfig *m_config = nullptr;

  // input filter (from m_config);
  InputFilter *m_inputFilter = nullptr;

  // state saved when screen saver activates
  BaseClientProxy *m_activeSaver = nullptr;

  BaseClientProxy *m_switchComputer = nullptr;
  double m_switchWaitDelay = 0.0;
  EventQueueTimer *m_switchWaitTimer = nullptr;
  EventQueueTimer *m_clipboardRetryTimer = nullptr;
  int m_clipboardRetries = 0;

  // delay for double-tap computer switching
  double m_switchTwoTapDelay = 0.0;

  // server computer
  deskflow::Computer *m_computer;

  IEventQueue *m_events = nullptr;
  size_t m_maximumClipboardSize = INT_MAX;
  ClientListener *m_clientListener = nullptr;
  Stopwatch m_switchTwoTapTimer;
  //! The computer the cursor came from on the last switch, and since when it is here
  const BaseClientProxy *m_cameFrom = nullptr;
  //! leaving failed and said so: it fails on every motion at the edge until it can
  bool m_cantLeaveReported = false;
  Stopwatch m_sinceArrival;

  // Name of computer broadcasting the keyboard events
  std::string m_keyboardBroadcastingComputers;

  //! The primary sends the mouse's own motion (PrimaryComputerRawMotionOnSecondary): in
  //! relative motion it replaces the accelerated one, until it stops coming
  bool m_rawMotionWorks = false;
  int m_motionsWithoutRaw = 0;

  //! The monitors each client said it has, by name
  std::map<std::string, std::vector<deskflow::Monitor>> m_clientMonitors;

  //! The client whose game has the mouse, if any
  BaseClientProxy *m_mouseCapturedBy = nullptr;
  //! The person let go of the mouse by hand while the game held it, until it lets go too
  bool m_mouseCaptureReleased = false;

  // all clients (including the primary client) indexed by name
  using ClientList = std::map<std::string, BaseClientProxy *>;
  using ClientSet = std::set<BaseClientProxy *>;
  ClientList m_clients;
  ClientSet m_clientSet;

  // all old connections that we're waiting to hangup
  using OldClients = std::map<BaseClientProxy *, EventQueueTimer *>;
  OldClients m_oldClients;

  // clipboard cache
  ClipboardInfo m_clipboards[kClipboardEnd];

  // used in hello message sent to the client
  NetworkProtocol m_protocol = NetworkProtocol::Barrier;

  // the sequence number of enter messages
  uint32_t m_seqNum = 0;

  // current mouse position (in absolute computer coordinates) on
  // whichever computer is active
  int32_t m_x;
  int32_t m_y;

  // last mouse deltas.  this is needed to smooth out double tap
  // on win32 which reports bogus mouse motion at the edge of
  // the computer when using low level hooks, synthesizing motion
  // in the opposite direction the mouse actually moved.
  int32_t m_xDelta = 0;
  int32_t m_yDelta = 0;
  int32_t m_xDelta2 = 0;
  int32_t m_yDelta2 = 0;

  int32_t m_xSaver;
  int32_t m_ySaver;

  // state for delayed computer switching
  int32_t m_switchWaitX;
  int32_t m_switchWaitY;

  int32_t m_switchTwoTapZone = 3;

  // common state for computer switch tests.  all tests are always
  // trying to reach the same computer in the same direction.
  Direction m_switchDir = Direction::NoDirection;

  bool m_switchTwoTapEngaged = false;
  bool m_switchTwoTapArmed = false;

  // relative mouse move option
  bool m_relativeMoves = false;

  // flag whether or not we have broadcasting enabled and the computers to
  // which we should send broadcasted keys.
  bool m_keyboardBroadcasting = false;

  // computer locking (former scroll lock)
  bool m_lockedToComputer = false;

  bool m_defaultLockToComputerState = false;
  bool m_disableLockToComputer = false;
  bool m_enableClipboard = true;

  // copying files
  std::unique_ptr<deskflow::files::FileHub> m_fileHub;
  uint64_t m_fileMaxBytes = 0;
  std::map<std::string, deskflow::files::FileList> m_filesIncoming; // by client, for the GUI
  std::unique_ptr<deskflow::files::FileProgress> m_fileProgress;
};
