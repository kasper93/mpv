JSON IPC
========

mpv can be controlled by external programs using the JSON-based IPC protocol.
It can be enabled by specifying the path to a unix socket or a named pipe using
the option ``--input-ipc-server``, or the file descriptor number of a unix socket
or a named pipe using ``--input-ipc-client``.
Clients can connect to this socket and send commands to the player or receive
events from it.

.. warning::

    This is not intended to be a secure network protocol. It is explicitly
    insecure: there is no authentication, no encryption, and the commands
    themselves are insecure too. For example, the ``run`` command is exposed,
    which can run arbitrary system commands. The use-case is controlling the
    player locally. This is not different from the MPlayer slave protocol.

Socat example
-------------

You can use the ``socat`` tool to send commands (and receive replies) from the
shell. Assuming mpv was started with:

::

    mpv file.mkv --input-ipc-server=/tmp/mpvsocket

Then you can control it using socat:

::

    > echo '{ "command": ["get_property", "playback-time"] }' | socat - /tmp/mpvsocket
    {"data":190.482000,"error":"success"}

In this case, socat copies data between stdin/stdout and the mpv socket
connection.

See the ``--idle`` option how to make mpv start without exiting immediately or
playing a file.

It's also possible to send input.conf style text-only commands:

::

    > echo 'show-text ${playback-time}' | socat - /tmp/mpvsocket

But you won't get a reply over the socket. (This particular command shows the
playback time on the player's OSD.)

Command Prompt example
----------------------

Unfortunately, it's not as easy to test the IPC protocol on Windows, since
Windows ports of socat (in Cygwin and MSYS2) don't understand named pipes. In
the absence of a simple tool to send and receive from bidirectional pipes, the
``echo`` command can be used to send commands, but not receive replies from the
command prompt.

Assuming mpv was started with:

::

    mpv file.mkv --input-ipc-server=\\.\pipe\mpvsocket

You can send commands from a command prompt:

::

    echo show-text ${playback-time} >\\.\pipe\mpvsocket

To be able to simultaneously read and write from the IPC pipe, like on Linux,
it's necessary to write an external program that uses overlapped file I/O (or
some wrapper like .NET's NamedPipeClientStream.)

You can open the pipe in PuTTY as "serial" device. This is not very
comfortable, but gives a way to test interactively without having to write code.

Protocol
--------

The protocol uses UTF-8-only JSON as defined by RFC-8259. Unlike standard JSON,
"\u" escape sequences are not allowed to construct surrogate pairs. To avoid
getting conflicts, encode all text characters including and above codepoint
U+0020 as UTF-8. mpv might output broken UTF-8 in corner cases (see "UTF-8"
section below).

Clients can execute commands on the player by sending JSON messages of the
following form:

::

    { "command": ["command_name", "param1", "param2", ...] }

where ``command_name`` is the name of the command to be executed, followed by a
list of parameters. Parameters must be formatted as native JSON values
(integers, strings, booleans, ...). Every message **must** be terminated with
``\n``. Additionally, ``\n`` must not appear anywhere inside the message. In
practice this means that messages should be minified before being sent to mpv.

mpv will then send back a reply indicating whether the command was run
correctly, and an additional field holding the command-specific return data (it
can also be null).

::

    { "error": "success", "data": null }

mpv will also send events to clients with JSON messages of the following form:

::

    { "event": "event_name" }

where ``event_name`` is the name of the event. Additional event-specific fields
can also be present. See `List of events`_ for a list of all supported events.

Because events can occur at any time, it may be difficult at times to determine
which response goes with which command. Commands may optionally include a
``request_id`` which, if provided in the command request, will be copied
verbatim into the response. mpv does not interpret the ``request_id`` in any
way; it is solely for the use of the requester. The only requirement is that
the ``request_id`` field must be an integer (a number without fractional parts
in the range ``-2^63..2^63-1``). Using other types is deprecated and will
currently show a warning. In the future, this will raise an error.

For example, this request:

::

    { "command": ["get_property", "time-pos"], "request_id": 100 }

Would generate this response:

::

    { "error": "success", "data": 1.468135, "request_id": 100 }

If you don't specify a ``request_id``, command replies will set it to 0.

All commands, replies, and events are separated from each other with a line
break character (``\n``).

If the first character (after skipping whitespace) is not ``{``, the command
will be interpreted as non-JSON text command, as they are used in input.conf
(or ``mpv_command_string()`` in the client API). Additionally, lines starting
with ``#`` and empty lines are ignored.

Currently, embedded 0 bytes terminate the current line, but you should not
rely on this.

Data flow
---------

Currently, the mpv-side IPC implementation does not service the socket while a
command is executed and the reply is written. It is for example not possible
that other events, that happened during the execution of the command, are
written to the socket before the reply is written.

This might change in the future. The only guarantee is that replies to IPC
messages are sent in sequence.

Also, since socket I/O is inherently asynchronous, it is possible that you read
unrelated event messages from the socket, before you read the reply to the
previous command you sent. In this case, these events were queued by the mpv
side before it read and started processing your command message.

If the mpv-side IPC implementation switches away from blocking writes and
blocking command execution, it may attempt to send events at any time.

You can also use asynchronous commands, which can return in any order, and
which do not block IPC protocol interaction at all while the command is
executed in the background.

Asynchronous commands
---------------------

Command can be run asynchronously. This behaves exactly as with normal command
execution, except that execution is not blocking. Other commands can be sent
while it's executing, and command completion can be arbitrarily reordered.

The ``async`` field controls this. If present, it must be a boolean. If missing,
``false`` is assumed.

For example, this initiates an asynchronous command:

::

    { "command": ["screenshot"], "request_id": 123, "async": true }

And this is the completion:

::

    {"request_id":123,"error":"success","data":null}

By design, you will not get a confirmation that the command was started. If a
command is long running, sending the message will not lead to any reply until
much later when the command finishes.

Some commands execute synchronously, but these will behave like asynchronous
commands that finished execution immediately.

Cancellation of asynchronous commands is available in the libmpv API, but has
not yet been implemented in the IPC protocol.

Commands with named arguments
-----------------------------

If the ``command`` field is a JSON object, named arguments are expected. This
is described in the C API ``mpv_command_node()`` documentation (the
``MPV_FORMAT_NODE_MAP`` case). In some cases, this may make commands more
readable, while some obscure commands basically require using named arguments.

Currently, only "proper" commands (as listed by `List of Input Commands`_)
support named arguments.

Commands
--------

In addition to the commands described in `List of Input Commands`_, a few
extra commands can also be used as part of the protocol:

``client_name``
    Return the name of the client as string. This is the string ``ipc-N`` with
    N being an integer number.

``get_time_us``
    Return the current mpv internal time in microseconds as a number. This is
    basically the system time, with an arbitrary offset.

``get_property``
    Return the value of the given property. The value will be sent in the data
    field of the replay message.

    Example:

    ::

        { "command": ["get_property", "volume"] }
        { "data": 50.0, "error": "success" }

``get_property_string``
    Like ``get_property``, but the resulting data will always be a string.

    Example:

    ::

        { "command": ["get_property_string", "volume"] }
        { "data": "50.000000", "error": "success" }

``set_property``
    Set the given property to the given value. See `Properties`_ for more
    information about properties.

    Example:

    ::

        { "command": ["set_property", "pause", true] }
        { "error": "success" }

``set_property_string``
    Alias for ``set_property``. Both commands accept native values and strings.

``observe_property``
    Watch a property for changes. If the given property is changed, then an
    event of type ``property-change`` will be generated

    Example:

    ::

        { "command": ["observe_property", 1, "volume"] }
        { "error": "success" }
        { "event": "property-change", "id": 1, "data": 52.0, "name": "volume" }

    .. warning::

        If the connection is closed, the IPC client is destroyed internally,
        and the observed properties are unregistered. This happens for example
        when sending commands to a socket with separate ``socat`` invocations.
        This can make it seem like property observation does not work. You must
        keep the IPC connection open to make it work.

``observe_property_string``
    Like ``observe_property``, but the resulting data will always be a string.

    Example:

    ::

        { "command": ["observe_property_string", 1, "volume"] }
        { "error": "success" }
        { "event": "property-change", "id": 1, "data": "52.000000", "name": "volume" }

``unobserve_property``
    Undo ``observe_property`` or ``observe_property_string``. This requires the
    numeric id passed to the observed command as argument.

    Example:

    ::

        { "command": ["unobserve_property", 1] }
        { "error": "success" }

``request_log_messages``
    Enable output of mpv log messages. They will be received as events. The
    parameter to this command is the log-level (see ``mpv_request_log_messages``
    C API function).

    Log message output is meant for humans only (mostly for debugging).
    Attempting to retrieve information by parsing these messages will just
    lead to breakages with future mpv releases. Instead, make a feature request,
    and ask for a proper event that returns the information you need.

``enable_event``, ``disable_event``
    Enables or disables the named event. Mirrors the ``mpv_request_event`` C
    API function. If the string ``all`` is used instead of an event name, all
    events are enabled or disabled.

    By default, most events are enabled, and there is not much use for this
    command.

``get_version``
    Returns the client API version the C API of the remote mpv instance
    provides.

    See also: ``DOCS/client-api-changes.rst``.

UTF-8
-----

Normally, all strings are in UTF-8. Sometimes it can happen that strings are
in some broken encoding (often happens with file tags and such, and filenames
on many Unixes are not required to be in UTF-8 either). This means that mpv
sometimes sends invalid JSON. If that is a problem for the client application's
parser, it should filter the raw data for invalid UTF-8 sequences and perform
the desired replacement, before feeding the data to its JSON parser.

mpv will not attempt to construct invalid UTF-8 with broken "\u" escape
sequences. This includes surrogate pairs.

JSON extensions
---------------

The following non-standard extensions are supported:

    - a list or object item can have a trailing ","
    - object syntax accepts "=" in addition of ":"
    - object keys can be unquoted, if they start with a character in "A-Za-z\_"
      and contain only characters in "A-Za-z0-9\_"
    - byte escapes with "\xAB" are allowed (with AB being a 2 digit hex number)

Example:

::

    { objkey = "value\x0A" }

Is equivalent to:

::

    { "objkey": "value\n" }

Alternative ways of starting clients
------------------------------------

You can create an anonymous IPC connection without having to set
``--input-ipc-server``. This is achieved through a mpv pseudo scripting backend
that starts processes.

You can put ``.run`` file extension in the mpv scripts directory in its  config
directory (see the `FILES`_ section for details), or load them through other
means (see `Script location`_). These scripts are simply executed with the OS
native mechanism (as if you ran them in the shell). They must have a proper
shebang and have the executable bit set.

When executed, a socket (the IPC connection) is passed to them through file
descriptor inheritance. The file descriptor is indicated as the special command
line argument ``--mpv-ipc-fd=N``, where ``N`` is the numeric file descriptor.

The rest is the same as with a normal ``--input-ipc-server`` IPC connection. mpv
does not attempt to observe or other interact with the started script process.

This does not work in Windows yet.
