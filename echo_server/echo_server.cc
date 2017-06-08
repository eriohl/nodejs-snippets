
#include <node.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>


namespace echo_server {


static uv_tcp_t server_;

//
// We use [loop_] != NULL to indicate that the echo server already has been
// started.
//
static uv_loop_t *loop_ = NULL;


static void error(const char *prefix, int status)
{
  ::fprintf(stderr, "%s: %s.\n", prefix, uv_strerror(status));
}


static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  // Allocates a buffer to read input into. Buffer is then passed to [read_cb].
  // Buffer is not reused but is always released on call to [read_cb].
  //
  // [suggested_size] is just advisory (usually 64 KiB). We can allocate a
  // smaller or a larger size.
  //
  // If the allocation fails ([buf]->base == NULL) and error is passed to
  // read_cb regardless of what size we set.
  
  *buf = uv_buf_init(
    reinterpret_cast<char *>(::malloc(suggested_size)), suggested_size
    );
}


struct write_data
{
  uv_write_t req;
  uv_buf_t buf;
};


static void free_write_data(write_data *wd)
{
  ::free(wd->buf.base);
  ::free(wd);
}


static void write_cb(uv_write_t *req, int status)
{
  // Called when data has been written to socket.
  
  write_data *wd = reinterpret_cast<write_data *>(req->data);
  free_write_data(wd);

  if (status != 0) error("Error on writing client stream", status);
}


static void close_and_free(uv_stream_t* client)
{
  uv_close(reinterpret_cast<uv_handle_t *>(client), NULL);
  ::free(client);
}


static void read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t *in_buf)
{
  // Called when data has been read from socket or there is an error.
  //
  // [nread] contains the number of bytes read if >= 0 and error is < 0. [nread]
  // of 0 does not mean that the socket has been closed.
  //
  // We are expected to close the socket in case of error.
  //
  // Data is read into [buf]->base. Buffer has been allocated by previous call
  // [alloc_cb]. We are expected to free this buffer before returning. Note that
  // [buf]->base might be NULL.
  
  char *in_data = in_buf->base;
  
  if (nread > 0)
  {
    // Send echo response. We reuse the buffer passed to the read callback and
    // free it once the write has been completed.
    
    // uv_write_t::data is used to keep state associated with the write
    // operation.
    
    write_data *wd =
      reinterpret_cast<write_data *>(::malloc(sizeof(write_data)));
    wd->req.data = wd;
    wd->buf = uv_buf_init(in_data, nread);
    in_data = 0; // Don't free it now.
    
    int r = uv_write(&wd->req, stream, &wd->buf, 1, write_cb);
    if (r == 0)
    {
      // Write is pending. [write_cb] will be called on write completed.
    }
    else
    {
      // Write failed.
      //
      // Documentation is not explicit on this but assuming that there is no
      // call to [write_cb]
      
      free_write_data(wd);
      error("Error on writing client stream", r);
    }
  }
  else if (nread < 0)
  {
    if (nread != UV_EOF)
      error("Error on reading client stream", nread);

    close_and_free(stream);
  }
  
  if (in_data) ::free(in_data);
}


static void connection_cb(uv_stream_t * server, int status)
{
  if (status < 0)
  {
    error("Error on listening", status);
    return; // Assuming no connection to accept.
  }
  
  uv_tcp_t *client = reinterpret_cast<uv_tcp_t *>(::malloc(sizeof(uv_tcp_t)));
  uv_tcp_init(loop_, client);
  
  int r = uv_accept(server, reinterpret_cast<uv_stream_t *>(client));
  if (r == 0)
  {
    // Start reading. We continue reading until calling uv_read_stop() or
    // uv_close().
    
    r = uv_read_start(
      reinterpret_cast<uv_stream_t *>(client), alloc_cb, read_cb
      );
    if (r == 0)
    {
      // Reads are pending. [read_cb] will be called when data has been read.
    }
    else
    {
      close_and_free(reinterpret_cast<uv_stream_t *>(client));
      error("Error on reading client stream", r);
    }
  }
  else
  {
    ::free(client);
    error("Error on accepting client connection", r);
  }
}


static void start(const v8::FunctionCallbackInfo<v8::Value> &args)
{
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong number of arguments")));
    return;
  }

  if (!args[0]->IsNumber())
  {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong arguments")));
    return;
  }
  
  if (loop_)
  {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Already started")));
    return;
  }
  
  int port = args[0]->IntegerValue(); // Truncated
  
  uv_loop_t *loop = uv_default_loop(); // Node.js uses the default loop.
    
  struct sockaddr_in addr;
  int r = uv_ip4_addr("127.0.0.1", port, &addr);
  if (r == 0)
  {
    // Note: Is it not clear from documentation if when handle is opened other
    // then that there are no socket created on [uv_tcp_init]. So assuming it is
    // opened on successful call to [uv_tcp_bind] and thus is should be closed
    // on failed call to [uv_listen]
    
    uv_tcp_init(loop, &server_);
    r = uv_tcp_bind(&server_, reinterpret_cast<sockaddr *>(&addr), 0);
    if (r == 0)
    {
      r = uv_listen(
        reinterpret_cast<uv_stream_t *>(&server_), 0, connection_cb
        );
      if (r == 0)
      {
        // Success.

        loop_ = loop;
      }
      else
      {
        uv_close(reinterpret_cast<uv_handle_t *>(&server_), NULL);
        error("Error on listening", r);
      }
    }
    else
    {
      error("Error on binding", r);
    }
  }
  else
  {
    error("Error on parsing address", r);
  }
  
  if (r != 0)
  {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Failed to start")));
    return;
  }
}


static void init(v8::Local<v8::Object> exports, v8::Local<v8::Object> module)
{
  NODE_SET_METHOD(exports, "start", start);
}

NODE_MODULE(echo_server, init)


} // namespace echo_server
