def convertFile(binary_filename, header_filename, variable_name):
    with open(binary_filename, 'rb') as b:
        with open(header_filename, 'w') as h:
            h.write('''static unsigned char %s[] = { ''' % variable_name);
            stuff = b.read()
            h.write(',\n'.join('%d' % ord(stuff[i:i+1]) for i in range(len(stuff))))
            h.write('\n};\n')
