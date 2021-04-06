#include <brle.h>
#include <iterator>
#include <string>
#include <string_view>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>


static void brle_error( const char * const format, ... )
{
    va_list args;
    va_start( args, format );

    std::vfprintf( stderr, format, args );
    std::fputc( '\n', stderr );
    std::exit( 1 );
}

static void brle_errno( const char * const prefix )
{
    std::perror( prefix );
    std::exit( 1 );
}


// Helper for parsing posix style commandline arguments.
struct options
{
    options( const int argc, const char ** const argv )
        : argc( argc )
        , argv( argv )
        , index( 1 )    // Skip executable name
        , opt( nullptr )
    {}
    
    // Reads an option character.
    // Returns a '\0' when no more options are available, however there may be additional operands.
    char read_option()
    {
        if( ( index < argc ) && ( opt == nullptr || *opt == '\0' ) )
        {
            opt = argv[ index++ ];
            if( *opt != '-' || opt[ 1 ] == '\0' )
            {
                return '\0';    // End of option list, doesn't start with '-' or a single '-' detected
            }
            if( *++opt == '-' )
            {
                ++opt;
                return '\0';    // End of option list, '--' marker
            }
        }

        return opt ? *opt++ : '\0';
    }

    // Reads a option argument or operand
    // An empty std::string_view is returned when no more arguments are available.
    std::string_view read_argument()
    {
        const char * arg = opt;
        if( ( index < argc ) && ( arg == nullptr || *arg == '\0' ) )
        {
            arg = argv[ index++ ];
        }
        opt = nullptr;

        return arg ? std::string_view( arg ) : std::string_view();
    }

private:
    const int           argc;
    const char ** const argv;
    int                 index;
    const char *        opt;
};

template< typename T >
struct binary_input_file_iterator
{
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T *;
    using reference         = T &;
    using iterator_category = std::input_iterator_tag;

    binary_input_file_iterator( std::FILE * file = nullptr )
        : file( file )
    {
        if( file )
        {
            next();
        }
    }

    bool operator==( const binary_input_file_iterator & other ) const
    {
        if( file == nullptr || other.file == nullptr )
        {
            return file == other.file;
        }

        return file == other.file && std::ftell( file ) == std::ftell( other.file );
    }

    T &                          operator*()        { return value; }
    binary_input_file_iterator * operator->() const { return &value; }
    binary_input_file_iterator & operator++()       { next(); return *this; }
    binary_input_file_iterator   operator++( int )  { auto it = *this; next(); return it; }

private:
    std::FILE * file  = nullptr;
    T           value = T();

    void next()
    {
        assert( file != nullptr );

        if( std::fread( &value, sizeof( T ), 1, file ) )
        {
            return;
        }
        else if( std::feof( file ) )
        {
            file = nullptr;
            return;
        }

        if( const int err = std::ferror( file ) )
        {
            brle_errno( "Input" );
        }
    }
};

template< typename T >
struct binary_output_file_iterator
{
    using difference_type   = std::ptrdiff_t;
    using value_type        = void;
    using pointer           = void;
    using reference         = void;
    using iterator_category = std::output_iterator_tag;

    binary_output_file_iterator( std::FILE * file = nullptr )
        : file( file )
    {}

    T operator=( T value )
    {
        assert( file );

        if( std::fwrite( &value, sizeof( T ), 1, file ) != 1 )
        {

        }

        return value;
    }

    bool operator==( const binary_output_file_iterator & other ) const
    {
        if( file == nullptr || other.file == nullptr )
        {
            return file == other.file;
        }

        return file == other.file && std::ftell( file ) == std::ftell( other.file );
    }

    binary_output_file_iterator & operator*()       { return *this; }
    binary_output_file_iterator & operator++()      { return *this; }
    binary_output_file_iterator   operator++( int ) { return *this; }

private:
    std::FILE * const file = nullptr;
};


static void print_help()
{
    const char * const help =
        "blre v1.0.0\n"
        "\n"
        "A tool to compress or expand binary data using Run-Length Encoding.\n"
        "\n"
        "SYNOPSIS\n"
        "    brle [-e|-d] [-?] input output\n"
        "\n"
        "DESCRIPTION\n"
        "    blre reduces the size of its input by using a variant of the\n"
        "    Run-Length Encoding compression method that is optimized for binary data.\n"
        "\n"
        "    With this utility you can test the efficiency of the compression for your\n"
        "    use case or create a binary blobs that are going to be included in your\n"
        "    application or firmware.\n"
        "\n"
        "    The advantage of the RLE over other compression methods is that RLE can\n"
        "    compresses data in a single pass and does not require any buffering of the\n"
        "    input or output data. These properties may be a good fit for applications\n"
        "    that are tight on memory usage or require low latencies. However due to\n"
        "    simplicity of RLE the compression may not be as good as achieved by other\n"
        "    utilities.\n"
        "\n"
        "OPTIONS\n"
        "    -e  Encode input.\n"
        "    -d  Decode input.\n"
        "    -?  Shows this help.\n"
        "\n"
        "USAGE\n"
        "    Compress an input file and write the result to an output file.\n"
        "\n"
        "        brle -e file1 file2\n"
        "\n"
        "    The 'e' option is the default when no options are provided\n"
        "\n"
        "        brle file1 file2\n"
        "\n"
        "    When both of the 'e' and 'd' options are provided the last option is used.\n"
        "    The following example will decode the input.\n"
        "\n"
        "        brle -eded file1 file2\n"
        "\n"
        "    Expand RLE data from input file to output file\n"
        "\n"
        "        blre -d file1 file2\n"
        "\n"
        "    Use the output from another command as input, in this example 'cat'.\n"
        "\n"
        "        cat file1 | blre -e - file2\n"
        "\n"
        "    Expand from from input file to standard output\n"
        "\n"
        "        brle -d file -\n";

    std::puts( help );
}

static void encode( std::string_view in, std::string_view out )
{
    std::FILE * const in_file  = in == "-" ? stdin : std::fopen( std::string( in ).c_str(), "rb" );
    std::FILE * const out_file = out == "-" ? stdin : std::fopen( std::string( out ).c_str(), "wb" );

    if( in_file == nullptr )
    {
        brle_errno( "Input" );
    }

    if( out_file == nullptr )
    {
        brle_errno( "Output" );
    }

    auto in_begin  = binary_input_file_iterator< uint8_t >( in_file );
    auto in_end    = binary_input_file_iterator< uint8_t >();
    auto out_begin = binary_output_file_iterator< pg::brle::brle8 >( out_file );

    pg::brle::encode( in_begin, in_end, out_begin );
}

static void decode( std::string_view in, std::string_view out )
{
    std::FILE * const in_file  = in == "-" ? stdin : std::fopen( std::string( in ).c_str(), "rb" );
    std::FILE * const out_file = out == "-" ? stdout : std::fopen( std::string( out ).c_str(), "wb" );

    if( in_file == nullptr )
    {
        brle_errno( "Input" );
    }

    if( out_file == nullptr )
    {
        brle_errno( "Output" );
    }

    auto in_begin  = binary_input_file_iterator< pg::brle::brle8 >( in_file );
    auto in_end    = binary_input_file_iterator< pg::brle::brle8 >();
    auto out_begin = binary_output_file_iterator< uint8_t >( out_file );

    pg::brle::decode< binary_input_file_iterator< pg::brle::brle8 >,
                      binary_output_file_iterator< uint8_t >,
                      uint8_t >
                    ( in_begin, in_end, out_begin );
}


int main( const int argc, const char * argv[] )
{
    enum transformation : char { encode_ = 'e', decode_ = 'd' };

    transformation   direction = transformation::encode_;
    std::string_view input;
    std::string_view output;
    
    {
        options opts( argc, argv );
        for( char opt = opts.read_option() ; opt != '\0' ; opt = opts.read_option() )
        {
            switch( opt )
            {
            case 'e':
                direction = transformation::encode_;
                break;
                
            case 'd':
                direction = transformation::decode_;
                break;

            case '?':
                print_help();
                break;

            default:
                brle_error( "Unrecognized option '%c'. Use the '-?' option to get information about the usage.", opt );
            }
        }

        input  = opts.read_argument();
        output = opts.read_argument();
    }

    if( input.empty() )
    {
        brle_error( "No input provided. Use the '-?' option to get information about the usage." );
    }

    if( output.empty() )
    {
        brle_error( "No output provided. Use the '-?' option to get information about the usage." );
    }
    
    if( direction == transformation::encode_ )
    {
        encode( input, output );
    }
    else
    {
        decode( input, output );
    }

    return 0;
}