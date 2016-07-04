clear;

% #ask 1
% #satisfaction 2
% #return 3
% #add return 4
% #all received 5
% #add event 6
% #del event 7
% #del return 8
%c cols: time, server, message type, server epoch, event, epoch
%send 1
%query 2
%add 3
%del 4
%s cols: time, type, time, step, event
name='proudp';
c=csvread(sprintf('udp/%s_c.log',name));
s{1}=csvread(sprintf('udp/%s_0.log',name));
s{2}=csvread(sprintf('udp/%s_1.log',name));
s{3}=csvread(sprintf('udp/%s_2.log',name));

c3 = c;
c3(:,1)=(c(:,1)-min(c(:,1)))/2.6e6;
logudp = (10736234033676071-c(1,1))/2.6e6;
left = logudp-70;
right = logudp+300;

figure;
hold all;
xlim([min(c3(:,1))-1, max(c3(:,1))]+1);
ee = unique(c(:,5));
marks = {'^b','vk','xr','og'};
for e2 = 1:length(ee); %0:5,
    e = ee(e2);    
    c2 = c3(c3(:,5)==e,:);

    for i=1:size(c2,1)
        c2i = c2(i,:);
        if (c2i(3)~=2) %not a satisfaction
            continue;
        end

        y = c2i(2);
        if (e>2)
            pattern = marks{4};
            plot(c2i(1)-left, y, pattern, 'MarkerSize',8, 'LineWidth', 2);
        else
            pattern = marks{y+1};
            plot(c2i(1)-left, y, pattern, 'MarkerSize',9, 'LineWidth', 2);
        end
        
    end
end

x=csvread(sprintf('udp/%s_trace2.txt',name));
x(x(:,2)==3,:)=[];
timemin = min(x(:,1));
timemax = max(x(:,1));
t=[timemin:0.01:(timemax+0.2)];

%figure;
%hold all;

server=1; proto=6;
y=x(and(x(:,2)==server, x(:,4)==proto),:);
for i=2:length(t), 
    ti1 = t(i-1); 
    ti2 = t(i); 
    m1(i)=sum(x(and(y(:,1)>=ti1,y(:,1)<ti2),3)); 
end

server=2; proto=6;
y=x(and(x(:,2)==server, x(:,4)==proto),:);
clear m;
for i=2:length(t), 
    ti1 = t(i-1); 
    ti2 = t(i); 
    m2(i)=sum(x(and(y(:,1)>=ti1,y(:,1)<ti2),3)); 
end

server=1; proto=17;
y=x(and(x(:,2)==server, x(:,4)==proto),:);
traceudp = y(1,1);
clear m;
for i=2:length(t), 
    ti1 = t(i-1); 
    ti2 = t(i); 
    m3(i)=sum(x(and(y(:,1)>=ti1,y(:,1)<ti2),3)); 
end

bias = logudp - traceudp*1e3;
tracex = (t(1:(length(t)-1))+t(2:length(t)))/2*1e3+bias;
tracex = tracex - left;
maxm = max([m1 m2 m3])*1.1;
plot(tracex, m1(2:length(m1))/maxm, '-b', 'LineWidth', 2)
plot(tracex, 1 + m2(2:length(m2))/maxm, '-k', 'LineWidth', 2)
plot(tracex, 2 + m3(2:length(m3))/maxm, '-r', 'LineWidth', 2)

right = right- left;
left = 0;
set(gca,'YTick', [0 1 2])
set(gca,'YTickLabel', {'TCP1','TCP2','UDP'})
%ylabel('Connection Throughput');
top = 3;
ylim([-0.2 top]);
xlim([left, right]);
xlabel('Time (ms)');
set(findall(gcf,'type','text'),'fontSize',13)
set(findobj(gcf, 'type','axes'),'fontsize',13)
set(gcf,'OuterPosition',[500,500,375,360])

vgap = 0.25;
hgap = 130;
top = top -0.1;
texts = {'Congestion 1'; 'Congestion 2'; 'HH-proactive'; 'HH-reactive'};
for i = 1:4
    text(right-hgap, top - vgap * (4-i), texts{i}, 'fontsize',12)
    plot(right-hgap - 30, top - vgap * (4-i), marks{i}, 'MarkerSize',9, 'LineWidth', 2);
end
