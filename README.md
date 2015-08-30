# udp p2p communication example

## Protocol

Protocol message is a key-value string terminated with '\n'.

    client -> server
    login: user-name
    get-user-list:
    talk-to: user2-name

    server -> client
    open-channel: user-name ip:port
    user-info: user-name ip:port
    response: status description(optional)

    client <-> client
    talk-shake: user-name

## Server Side

    ./p2pd port

## Client Side

    ./p2p-client <server> <port>

## Client interactive example

    login: user1
    -> response: success

    get-user-list:
    -> user-list: user1 user2

    talk-to: user2
    -> user-info: user2 user2-ip:port
    -> ... talk-shake ...
    -> p2p connection established


