#' Model configuration write function
#'
#' This function reads a IBM configuration file and returns a list object in R. Where each element is a command and subcommand from the configuration file
#'
#' @author Craig Marsh
#' @param file the name of the input file containing model configuration
#' @param path Optionally, the path to the file
#' @param fileEncoding Optional, allows the R-library to read in files that have been encoded in alternative UTF formats, see the manual for the error message that would indicate when to use this switch.
#' @export
#'
"extract.ibm.file" <-
    function(file, path = "", fileEncoding = "", quiet = FALSE) {
    ## if no path specified look in current directory
    if (missing(path)) 
        path <- ""
    ## get the list that lins subcommands to their type.
    ibm_list = get.unique_subcommands_list();
    filename = make.filename(path = path, file = file)
    file = convert.to.lines(filename, fileEncoding = fileEncoding)
    ## remove white space at the beginning of a subcommand or command e.g
    while (any(regexpr(" ", file) == 1)) {
        index <- regexpr(" ", file) == 1
        file <- ifelse(index, substring(file, 2), file)
    }
    ## Remove any lines that begin with a #
    file <- file[substring(file, 1, 1) != "#"]
    ## Find and remove any lines that begin or end with { or } which is also a comment
    index1 <- ifelse(substring(file, 1, 2) == "/*", 1:length(file), 0)
    index2 <- ifelse(substring(file, 1, 2) == "*/", 1:length(file), 0)
    index1 <- index1[index1 != 0]
    index2 <- index2[index2 != 0]
    if (length(index1) != length(index2)) 
        stop(paste("Error in the file ", filename, ". Cannot find a matching '/*' or '*/'", 
            sep = ""))
    if (length(index1) > 0 || length(index2) > 0) {
        index <- unlist(apply(cbind(index1, index2), 1, function(x) seq(x[1], 
            x[2])))
        file <- file[!is.in(1:length(file), index)]
    }
    
    ## strip any remaining comments
    file <- ifelse(regexpr("#", file) > 0, substring(file, 1, regexpr("#", file) - 1), file)
    file <- file[file != ""]
    #if (substring(file[1], 1, 1) != "@") 
    #    stop(paste("Error in the file ", filename, ". Cannot find a '@' at the begining of the file", sep = ""))
        
    ## utiltiy function for stripping tabs and spaces out ot input values   
   
    ## try and convert tabs to spaces        
    file <- as.vector(tapply(file, 1:length(file),strip))
    
 
    blocks = get.lines(file, starts.with = "\\@", fixed = F)
    includes = get.lines(file, starts.with = "\\!", fixed = F)
    
    ## create a labels for blocks that do not take a label following the @block statement
    exception_blocks = c("model","categories")
    ## a list of tables that don't have headers    
    non_header_tables = c("obs","data","error_values","table", "layer")
    ## there are three types of tables, 1) tables with headers (Instant mortality) 2) tables with row labels (observations and error values) and 3)tables that are just a matrix (ageing error)
    ans <- list()
    if(!quiet)
      print(paste("The 'ibm' input parameter file has", length(file[substring(file, 1, 1) == "@"]), "commands, and", length(file), "lines"))
    CommandCount <- 0
    ## A global variable to tell us if we are still inputing a table
    in_table = FALSE;
    label_name = 1; ## this will be the default label if no user defined label is specified
    for (i in 1:length(file)) {
          temp <- string.to.vector.of.words(file[i])
          check_inputs = check_short_hand(temp)
          ## expand numeric shorthand
          if (any(check_inputs$numeric) && substr(Command,1,3) != "est") {
            vals = eval(parse(text = temp[check_inputs$numeric]))
            eval(parse(text = "temp = paste(c(temp[!check_inputs$numeric],as.character(vals)), collapse = ' ')"))             
            temp = string.to.vector.of.words(temp)
          }
          ## expand String shorthand
          if (any(check_inputs$string)) {
            ## Do nothing for now, this one needs more thought
          }
          ## Check if it is a beginning of a block
          if (substring(temp[1], 1, 1) == "@") {              
              if(is.na(temp[2])) {
                temp[2]= paste(substr(temp[1],2, 1000), "_",label_name , sep = "")
                label_name = label_name + 1;
              }      
              ## create a block
              header = 1
              CommandCount <- CommandCount + 1
              Command <- substring(temp[1], 2)
              if (!is.in(Command,exception_blocks)) {
                ## Create a label for the block
                Command = paste(Command, "[", temp[2], "]", sep = "");
                if(!quiet)
                  print(temp[2])
             }
             next; ## if we come across a block we either give it a label and move on or just move on if it is an exception block
          }
          ## Check for an include statement
          if (substring(temp[1], 1, 1) == "!") {  
            if (length(ans[[temp[1]]]$values) > 0) {
              ans[[temp[1]]]$values = c(ans[[temp[1]]]$values,temp[2])
            } else {
              ans[[temp[1]]]$values = temp[2]
            }
            next; ## skip out of this level
          }
          ## only two types of subcommands tables and vectors
          if (in_table || any(ibm_list$command == temp[1])) {
            type = "table_label"
          } else {
            type = "vector"
          }
          ## Check if it is a valid type/subcommand
          if (type == "vector") {
            ## deal with a vector subcommand
            ans[[Command]][[temp[1]]] = list("value" = temp[-1])              
          } else if ((type == "table_label") || in_table) {
            ## deal with a table input. the biggest pain in ithe ass
            in_table = TRUE;
            if (header == 1) {
              Label = temp[2];
              table_list = list();
              mat = matrix();
            } else {
              ## try the other cases
              if (header == 2 && !Label %in% non_header_tables) {
               ## We need to read in the header labels for the table
               Colnames = temp;                   
               ## intialise temp list object for storing info into the table                 
              } else if ((header > 2 && temp[1] != "end_table") || (Label %in% non_header_tables && temp[1] != "end_table")){
                ## create a temp list which will be the same as 
                if (!Label %in% non_header_tables) {
                  ## if not an observational table
                  for ( j in 1:length(Colnames)) {
                    table_list[[Colnames[j]]] = c(table_list[[Colnames[j]]],temp[j])
                  }
                } else {
                  ## else make an exception for an observational table and ageing error
                  if (Label == "table") { # then this is a ageing error matrix                      
                    if (header == 2) {
                       mat = temp;
                    } else {
                       mat = rbind(mat, temp); 
                    }
                  } else if (Label == "layer") {
                    if (header == 2) {
                       mat = temp;
                    } else {
                       mat = rbind(mat, temp); 
                    }                  
                  } else {  
                    table_list[[temp[1]]] = temp[-1];
                  }
                }
              } else if (length(ibm_list$type[ibm_list$command == temp[1]] == "end_table") > 0) {
              ## an initial check to prevent logical(0) in the condition
                if (temp[1] == "end_table") {
                 ## we are leaving the table inputs
                 in_table = FALSE;  
                 header = 1;
                 if (!Label %in% c("table","layer")) {
                  for (k in 1:length(names(table_list))) {
                    eval(parse(text= paste("ans[['",Command,"']]$Table$",Label,"[['",names(table_list)[k], "']] = table_list$'",names(table_list)[k] ,"'" ,sep="")));
                  }
                 } else {
                  dimnames(mat) = NULL;
                  eval(parse(text= paste("ans[['",Command,"']]$Table$",Label," = mat" , sep = "")));
                 }
                 next;
                }
             }
          }
          header = header + 1;
          } else {
            stop(paste("Haven't come up with R code to deal with this subcommand", temp[1]))
          }
      }
   ans;
}