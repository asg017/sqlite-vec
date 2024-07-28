


data/:
	mkdir -p $@

data/sift: data/
	curl -o data/sift.tar.gz ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz
	tar -xvzf data/sift.tar.gz -C data/
	rm data/sift.tar.gz

data/gist: data/
	curl -o data/gist.tar.gz ftp://ftp.irisa.fr/local/texmex/corpus/gist.tar.gz
	tar -xvzf data/gist.tar.gz -C data/
	rm data/gist.tar.gz
