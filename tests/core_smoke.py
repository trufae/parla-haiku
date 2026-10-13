#!/usr/bin/env python3
"""Offline integration test of the actual packaged core. Never contacts a relay."""
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading

with tempfile.TemporaryDirectory(prefix="parla-core-") as directory:
    server = subprocess.Popen([os.path.abspath(sys.argv[1])], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        env={**os.environ, "DC_ACCOUNTS_PATH": directory})
    replies = queue.Queue()
    def reader():
        for line in server.stdout:
            replies.put(json.loads(line))
    threading.Thread(target=reader, daemon=True).start()
    sequence = 0
    def call(method, *params):
        global sequence
        sequence += 1
        server.stdin.write(json.dumps(dict(jsonrpc="2.0", id=sequence, method=method, params=params)) + "\n")
        server.stdin.flush()
        reply = replies.get(timeout=45)
        assert reply["id"] == sequence, reply
        assert "error" not in reply, (method, reply)
        return reply.get("result")
    def send_offline(account, chat, data):
        try:
            call("send_msg", account, chat, data)
        except AssertionError as error:
            assert "No self addr configured" in str(error), error
        else:
            raise AssertionError("Unconfigured profile unexpectedly sent mail")
        ids = call("get_message_ids", account, chat, False, False)
        return ids[-1]
    try:
        info = call("get_system_info")
        assert info
        account = call("add_account")
        call("set_config", account, "displayname", "Haiku 日本語")
        call("set_config", account, "addr", "haiku@example.org")
        assert call("get_config", account, "displayname") == "Haiku 日本語"
        contact = call("create_contact", account, "recipient@example.org", "Recipient")
        chat = call("create_chat_by_contact_id", account, contact)
        text = "Native Haiku → Delta Chat\nCatalà 日本語"
        message = send_offline(account, chat, dict(text=text))
        data = call("get_message", account, message)
        assert data["text"] == text and data["chatId"] == chat
        ids = call("get_message_ids", account, chat, False, False)
        assert message in ids
        assert call("get_messages", account, [message])[str(message)]["kind"] == "message"
        chats = call("get_chatlist_entries", account, 0, None, None)
        assert chat in chats
        assert call("get_chatlist_items_by_entries", account, chats)[str(chat)]["kind"] == "ChatListItem"
        attachment = os.path.join(directory, "hello.txt")
        with open(attachment, "w") as output:
            output.write(text)
        msg = send_offline(account, chat, dict(text="Attachment", file=attachment))
        saved = os.path.join(directory, "saved.txt")
        call("save_msg_file", account, msg, saved)
        with open(saved) as source:
            assert source.read() == text
        call("delete_messages", account, [message])
        assert message not in call("get_message_ids", account, chat, False, False)
        print("core: profile, Unicode, failed offline sends, chat/message batches, attachment and deletion OK")
    finally:
        server.stdin.close()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
