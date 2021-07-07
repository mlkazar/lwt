# ThreadPipe Package

This document describes the ThreadPipe package, which implements a unidirectional pipe.

## ThreadPipe API
A pipe is created by calling `new ThreadPipe`.  One user of the pipe writes data to the pipe by calling the `::write` method, and the other side of the pipe reads the data by calling the `::read` method.

When the last bytes have been written, the writer calls `::eof` , which will cause any reads to terminate.  The reader can call `::atEof`, which will return true iff eof has been set by the writer.

The API is so defined:

`::reset()` resets the pipe to be empty, without EOF being set.

`::write(const char *bufferp, int32_t count)` writes count bytes into the pipe.  The pipe can only store a fixed number of bytes, so this call may block if the number of bytes written exceeds the amount of space for unread data remaining in the pipe.

`::read(char *bufferp, int32_t count)`reads up to count bytes into the specified buffer.  The read call returns all available bytes, and only waits if there are no bytes available to return.  If eof has been set, and all bytes have been read, the read call returns 0 bytes, and atEof returns true.

`::eof()` sets eof on the pipe, terminating any pending reads waiting for data.

`::atEof()` returns true iff EOF was set.

`::waitForEof()` discards incoming data repeatedly until all the written data has been consumed and the writer called EOF.

`::count()` returns the number of bytes available for reading.

## Internal Locking

Nothing too complex here.  The ThreadPipe structure contains a simple mutex and associated conditional variable.  The condition variable is waited on when a reader needs data, or a writer needs space.

## Improvements

Allow specifying the buffer size.

## Usage Examples

Here's an example of a writer that copies a string into a pipe, and a reader that receives the data and prints it out.

```
void writer(ThreadPipe *pipep, FILE *filep)
{
	char tc;
	while(1) {
		tc = fgetc(filep);
		if (tc < 0)
			break;
		pipep->write(&tc, 1);
	}
	pipep->eof();
}
```

and the reader looks like this:
```
void reader(ThreadPipe *pipep)
{
	char tc;
	int32_t code;

	while(1) {
		code = pipep->read(&tc, 1);
		if (code == 0)
			break;
		printf("%c", tc);
	}
	printf("\n");
}
```

## Warnings

Note that write can block until a reader actually reads its data.  Also note that read only blocks if there is no data available to read, and its size parameter is simply a maximum number of bytes that can be read in a single call.
